#include "tokenizer.h"

#include "alloc.h"
#include "array.h"
#include "error.h"
#include "hashmap.h"
#include "str.h"
#include "token.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TOKENIZER_LINE_START 1

struct tokenizer {
    allocator     *parent;
    allocator     *strings;
    allocator     *arena; // lifetime = same as tokenizer
    allocator     *transient;
    tokenizer_opts opts;

    char_csized    input; // also in opts
    str            file;
    hashmap       *defines; // str hset

    u32            line;
    u32            pos;
    u32            col;

    token_array    backtrack;
    char_array     buf;
    u32            square_depth; // nesting depth of [ ] (to avoid greedy ]] inside type args)
};

// -- statics --

static void tok_error(tokenizer *self, tokenizer_error *err, tl_error_tag tag) {
    err->tag  = tag;
    err->file = alloc_strdup(self->arena, str_cstr(&self->file)); // lifetime = tokenizer's lifetime
    err->line = self->line;
    err->col  = self->col;
}

// -- allocation and deallocation --

tokenizer *tokenizer_create(allocator *alloc, tokenizer_opts const *opts) {
    tokenizer *self = alloc_calloc(alloc, 1, sizeof(tokenizer));

    self->parent    = alloc;
    self->strings   = alloc; // same as parent
    self->arena     = arena_create(alloc, 4096);
    self->transient = arena_create(alloc, 4096);
    self->opts      = *opts;
    self->input     = self->opts.input;
    self->file      = str_init(self->parent, self->opts.file); // parent's lifetime

    self->defines   = hset_create(self->strings, 8);

    self->pos       = 0;
    self->line      = TOKENIZER_LINE_START;
    self->col       = 0;

    self->buf       = (char_array){.alloc = alloc};
    self->backtrack = (token_array){.alloc = alloc};
    array_reserve(self->buf, 32);
    array_reserve(self->backtrack, 8);
    self->square_depth = 0;

    // load defines set
    forall(i, self->opts.defines) {
        str_hset_insert(&self->defines, self->opts.defines.v[i]);
    }

    return self;
}

void tokenizer_destroy(tokenizer **self) {
    str_deinit((*self)->parent, &(*self)->file);

    hset_destroy(&(*self)->defines);

    arena_destroy(&(*self)->arena);
    arena_destroy(&(*self)->transient);

    array_free((*self)->backtrack);
    array_free((*self)->buf);

    allocator *parent = (*self)->parent;
    alloc_invalidate(*self);
    alloc_free(parent, *self);
    *self = null;
}

// -- parsing --

static void replace_token(allocator *alloc, token *tok, token_tag tag) {
    token_deinit(alloc, tok);
    token_init(tok, tag);
}

static void replace_token_s(allocator *alloc, token *tok, token_tag tag, char const *s) {
    token_deinit(alloc, tok);
    token_init_s(alloc, tok, tag, s);
}

static void replace_token_sn(allocator *alloc, token *tok, token_tag tag, char const *s, size_t len) {
    token_deinit(alloc, tok);
    token_init_sn(alloc, tok, tag, s, len);
}

static void remove_number_extras(token *tok) {
    // remove '_' characters from string
    if (!tok->s) return;
    ptrdiff_t len   = (ptrdiff_t)strlen(tok->s);
    char     *start = tok->s;
    // skip initial characters
    while ('_' == *start && start - tok->s < len) start++;
    len -= (start - tok->s);
    tok->s  = start;

    char *p = tok->s;
    while (p - tok->s < len) {
        if ('_' == *p && p - tok->s < len) {
            memmove(p, p + 1, len - (p - tok->s) - 1);
            --len;
        } else {
            ++p;
        }
    }
    tok->s[len] = '\0';
}

static void advance_line(tokenizer *self) {
    self->line++;
    self->col = 0;
}

static void advance_pos(tokenizer *self) {
    self->col++;
    self->pos++;
}

static void advance_pos_n(tokenizer *self, i32 n) {
    if (n < 0) {
        if (self->col >= (u32)labs(n)) self->col += n;
        if (self->pos >= (u32)labs(n)) self->pos += n;
    } else {
        self->col += n;
        self->pos += n;
    }
}

static void reverse_pos(tokenizer *self) {
    if (self->col > 0) self->col--;
    else if (self->line > 0) self->line--; // reversing to previous line

    if (self->pos > 0) self->pos--;
}

static char next_char(tokenizer *self) {
    self->col++;
    char out = self->input.v[self->pos++];
    if (out == '\n') advance_line(self);
    return out;
}

static char peek_char(tokenizer *self, u32 pos) {
    if (pos >= self->input.size) return '\0';
    return self->input.v[pos];
}

static int is_end_of_line(tokenizer *self, char c) {
    if ('\n' == c) return 1;
    // self->pos points to character after c
    if ('\r' == c && self->pos < self->input.size && '\n' == self->input.v[self->pos]) return 1;
    return 0;
}

int tokenizer_next(tokenizer *self, token *out, tokenizer_error *out_err) {
    assert(out);
    out_err->tag = tl_err_ok;

    // support backtracking by parser
    if (self->backtrack.size) {
        *out = self->backtrack.v[--self->backtrack.size];
        return 0;
    }

    // ifdef/endif tracking
    int skip_depth = 0;

start:; // loop point for skip_depth > 0

    // state machine
    enum {
        start,

        in_bang,
        in_minus,
        in_plus,
        in_star,
        in_forward_slash,
        in_percent,
        in_equal,
        in_colon,
        in_dot,
        in_dot_2,
        in_ampersand,
        in_vertical_bar,
        in_open_square,
        in_close_square,

        start_number,
        start_number_sign,
        in_number,
        in_number_sign,
        in_number_exponent, // after seeing 'e' or 'E' in a number
        stop_number,

        start_string,
        in_string,
        in_string_backslash,
        in_string_backslash_unescaped,
        stop_string,

        start_char,
        in_char,
        in_char_backslash,
        stop_char,

        start_symbol,
        in_symbol,
        stop_symbol,

        in_s,
        start_ident, // a symbol, with restricted character set
        in_ident,

        start_comment,
        in_comment,
        stop_comment,

        start_hash_command,
        in_hash_command,
        in_ifc,
        stop_hash_command,

        error,
        stop,
    } state = start;

    enum {
        num_decimal,
        num_hex,
        num_binary,
    } number_format = num_decimal;

    enum {
        str_default,
        str_s_prefix,
    } string_format  = str_default;

    size_t const end = self->input.size;

    // starting position for number or symbol or indent
    size_t start_capture = 0;

    // return value, to be copied to *out
    token res = {0};

    while (1) {

        switch (state) {

        case start: {
            if (self->pos >= end) {
                tok_error(self, out_err, tl_err_eof);
                goto finish;
            }

            if (skip_depth < 0) {
                tok_error(self, out_err, tl_err_unexpected_endif);
                goto finish;
            }

            char const c = next_char(self);

            switch (c) {
            case '=': state = in_equal; break;
            case '&': state = in_ampersand; break;
            case '|': state = in_vertical_bar; break;
            case '-': state = in_minus; break;
            case '+': state = in_plus; break;
            case '*': state = in_star; break;
            case '/': state = in_forward_slash; break;
            case '%': state = in_percent; break;

            case '"': {
                state = start_string;
                continue;
            }

            case '\'': state = start_char; continue;

            case '\r':
            case '\n': continue;

            case 's':  state = in_s; continue;
            case '.':  state = in_dot; continue;
            case ':':  state = in_colon; continue;
            case '!':  state = in_bang; continue;
            case '#':  state = start_hash_command; continue;

            case '~':
                replace_token_s(self->strings, &res, tok_symbol, "~");
                state = stop;
                break;

            case ';':
                replace_token(self->strings, &res, tok_semicolon);
                state = stop;
                break;

            case ',':
                replace_token(self->strings, &res, tok_comma);
                state = stop;
                break;

            case '(':
                replace_token(self->strings, &res, tok_open_round);
                state = stop;
                break;

            case '{':
                replace_token(self->strings, &res, tok_open_curly);
                state = stop;
                break;

            case '[': state = in_open_square; continue;
            case ']': state = in_close_square; continue;

            case ')':
                replace_token(self->strings, &res, tok_close_round);
                state = stop;
                break;

            case '}':
                replace_token(self->strings, &res, tok_close_curly);
                state = stop;
                break;

            default:
                if (c >= '0' && c <= '9') {
                    // any digit starts a number
                    reverse_pos(self);
                    state = start_number;
                    continue;
                }

                if ((c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    reverse_pos(self);
                    state = start_ident;
                    continue;
                }

                if (c > 0x20 && c < 0x7f) {
                    // any other printable character starts a symbol
                    reverse_pos(self);
                    state = start_symbol;
                    continue;
                }

                // else skip char
                break;
            }
        } break;

        case in_bang: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_bang);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '=':
                replace_token(self->strings, &res, tok_bang_equal);
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_bang);
                state = stop;
                break;
            }
        } break;

        case in_colon: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_colon);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '=':
                replace_token(self->strings, &res, tok_colon_equal);
                state = stop;
                break;
            case ':':
                replace_token(self->strings, &res, tok_double_colon);
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_colon);
                state = stop;
                break;
            }
        } break;

        case in_open_square:
            if (self->pos == end) {
                self->square_depth++;
                replace_token(self->strings, &res, tok_open_square);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if ('[' == c && self->square_depth == 0) {
                replace_token(self->strings, &res, tok_double_open_square);
                state = stop;
            } else {
                reverse_pos(self);
                self->square_depth++;
                replace_token(self->strings, &res, tok_open_square);
                state = stop;
            }
            break;

        case in_close_square: {
            if (self->square_depth > 0) self->square_depth--;
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_close_square);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if (']' == c && self->square_depth == 0) {
                replace_token(self->strings, &res, tok_double_close_square);
                state = stop;
            } else {
                reverse_pos(self);
                replace_token(self->strings, &res, tok_close_square);
                state = stop;
            }
        } break;

        case in_dot: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_dot);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '.': state = in_dot_2; break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_dot);
                state = stop;
                break;
            }
        } break;

        case in_dot_2: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_dot);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '.':
                replace_token(self->strings, &res, tok_ellipsis);
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_dot);
                state = stop;
                break;
            }
        } break;

        case in_minus: {
            if (self->pos == end) {
                replace_token_s(self->strings, &res, tok_symbol, "-");
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '>':
                replace_token(self->strings, &res, tok_arrow);
                state = stop;
                break;
            case '=':
                replace_token_s(self->strings, &res, tok_symbol, "-=");
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_minus);
                state = stop;
                break;
            }
        } break;

        case in_plus: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_plus);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '=':
                replace_token_s(self->strings, &res, tok_symbol, "+=");
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_plus);
                state = stop;
                break;
            }
        } break;

        case in_star: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_star);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '=':
                replace_token_s(self->strings, &res, tok_symbol, "*=");
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_star);
                state = stop;
                break;
            }
        } break;

        case in_percent: {
            if (self->pos == end) {
                replace_token_s(self->strings, &res, tok_symbol, "%");
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '=':
                replace_token_s(self->strings, &res, tok_symbol, "%=");
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token_s(self->strings, &res, tok_symbol, "%");
                state = stop;
                break;
            }
        } break;

        case in_equal: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_equal_sign);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if ('=' == c) {
                // ==
                replace_token(self->strings, &res, tok_equal_equal);
                state = stop;
                goto finish;
            } else {
                reverse_pos(self);
                replace_token(self->strings, &res, tok_equal_sign);
                state = stop;
                goto finish;
            }
            break;

        } break;

        case in_ampersand: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_ampersand);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if ('&' == c) {
                // &&
                replace_token(self->strings, &res, tok_logical_and);
                state = stop;
                goto finish;
            } else if ('=' == c) {
                // &=
                replace_token_s(self->strings, &res, tok_symbol, "&=");
                state = stop;
                goto finish;
            } else {
                reverse_pos(self);
                replace_token(self->strings, &res, tok_ampersand);
                state = stop;
                goto finish;
            }

        } break;

        case in_vertical_bar: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_bar);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if ('|' == c) {
                // ||
                replace_token(self->strings, &res, tok_logical_or);
                state = stop;
                goto finish;
            } else if ('=' == c) {
                // |=
                replace_token_s(self->strings, &res, tok_symbol, "|=");
                state = stop;
                goto finish;
            } else {
                reverse_pos(self);
                replace_token(self->strings, &res, tok_bar);
                state = stop;
                goto finish;
            }

        } break;

        case in_forward_slash: {
            if (self->pos == end) {
                replace_token_s(self->strings, &res, tok_symbol, "/");
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            switch (c) {
            case '/': state = start_comment; continue;
            case '=':
                replace_token_s(self->strings, &res, tok_symbol, "/=");
                state = stop;
                break;
            default:
                reverse_pos(self);
                replace_token_s(self->strings, &res, tok_symbol, "/");
                state = stop;
                break;
            }

        } break;

        case start_number: {
            start_capture = self->pos;
            number_format = num_decimal;

            // Peek ahead to detect 0x, 0X, 0b, 0B prefixes
            if (peek_char(self, self->pos) == '0') {
                char second = peek_char(self, self->pos + 1);
                if (second == 'x' || second == 'X') {
                    number_format = num_hex;
                } else if (second == 'b' || second == 'B') {
                    number_format = num_binary;
                }
            }
            state = in_number;
        } break;

        case start_number_sign: {
            start_capture = self->pos;
            advance_pos(self); // capture the +/-
            state = in_number_sign;
        } break;

        case in_number: {
            if (self->pos == end) {
                state = stop_number;
                continue;
            }

            char const c = next_char(self);

            if (c >= '0' && c <= '9') {
                // For binary numbers, only allow 0 and 1
                if (number_format == num_binary && c > '1') {
                    reverse_pos(self);
                    state = stop_number;
                    continue;
                }
                continue;
            }

            // Exponent marker only valid for decimal numbers
            if ((c == 'e' || c == 'E') && number_format == num_decimal) {
                state = in_number_exponent;
                continue;
            }

            // Hex letters only for hex numbers
            if (number_format == num_hex) {
                if (c >= 'a' && c <= 'f') continue;
                if (c >= 'A' && c <= 'F') continue;
            }

            switch (c) {
            case '.':
                // Decimal point only for decimal numbers
                if (number_format != num_decimal) {
                    reverse_pos(self);
                    state = stop_number;
                    continue;
                }
                continue;
            case '_': continue;
            case 'z':
            case 'Z':
                // z/zu suffix: peek ahead for u/U
                if (peek_char(self, self->pos) == 'u' || peek_char(self, self->pos) == 'U') {
                    next_char(self); // consume the 'u'/'U' so token includes "zu"
                }
                state = stop_number;
                continue;
            case 'u':
            case 'U':
                // Unsigned suffix: consume it and stop the number
                state = stop_number;
                continue;
            case 'x':
            case 'X':
            case 'b':
            case 'B':
                // Only allow prefix chars at position 1 (second char of number)
                if (self->pos == start_capture + 2) continue;
                reverse_pos(self);
                state = stop_number;
                continue;

            default:
                // all other characters break a number
                reverse_pos(self);
                state = stop_number;
            }
        } break;

        case in_number_sign: {
            if (self->pos == end) {
                state = stop_symbol;
                continue;
            }

            // + or - not start of a number
            char const c = self->input.v[self->pos]; // Note: no increment
            switch (c) {
            case ' ':
            case ')': state = stop_symbol; break;
            default:  state = in_number; break;
            }
        } break;

        case in_number_exponent: {
            // After seeing 'e' or 'E', allow optional +/- followed by digits
            if (self->pos == end) {
                state = stop_number;
                continue;
            }

            char const c = next_char(self);

            // Allow a single + or - after exponent marker
            if (c == '+' || c == '-') {
                state = in_number;
                continue;
            }

            if (c >= '0' && c <= '9') {
                state = in_number;
                continue;
            }

            // Anything else breaks the number
            reverse_pos(self);
            state = stop_number;
        } break;

        case stop_number: {
            assert(self->pos >= start_capture);
            replace_token_sn(self->strings, &res, tok_number, self->input.v + start_capture,
                             self->pos - start_capture);
            remove_number_extras(&res);
            state = stop;
        } break;

        case start_symbol: {
            start_capture = self->pos;
            state         = in_symbol;
        } break;

        case in_s: {
            // 's' was consumed. If next char is '"', this is an s-string.
            if (self->pos < end && '"' == peek_char(self, self->pos)) {
                advance_pos(self); // skip the opening "
                string_format = str_s_prefix;
                state         = start_string;
                continue;
            }
            // Not s"...", treat 's' as start of identifier
            reverse_pos(self);
            state = start_ident;
            continue;
        } break;

        case start_ident: {
            start_capture = self->pos;
            state         = in_ident;
        } break;

        case in_symbol: {
            if (self->pos == end) {
                state = stop_symbol;
                continue;
            }

            char const c = next_char(self);
            switch (c) {
            case '(':
            case ')':
            case '{':
            case '}':
            case '[':
            case ']':
            case ' ':
            case '"':
            case ':':
            case ';':
            case ',':
            case '.':
            case '&':
            case '*':
            case '-':
            case '|':
            case '~':
                // these tokens break a symbol TODO there should be more
                // '=' does not break a symbol so we can support relations eg '>='
                reverse_pos(self);
                state = stop_symbol;
                break;
            default:
                // all invisible and extended characters break a symbol
                if (c < 0x20) { // c is signed so this catches c > 0x7f
                    reverse_pos(self);
                    state = stop_symbol;
                }
                break;
            }

        } break;

        case in_ident: {
            if (self->pos == end) {
                state = stop_symbol;
                continue;
            }

            char const c = next_char(self);
            if ((c == '_') || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c == '/'))
                break;

            // all other characters break an identifier
            reverse_pos(self);
            state = stop_symbol;

        } break;

        case stop_symbol: {
            assert(self->pos >= start_capture);
            replace_token_sn(self->strings, &res, tok_symbol, self->input.v + start_capture,
                             self->pos - start_capture);
            state = stop;

        } break;

        case start_comment:
            start_capture = self->pos;
            state         = in_comment;
            break;

        case in_comment: {
            if (self->pos == end) {
                state = stop_comment;
                continue;
            }
            char const c = next_char(self);
            if (is_end_of_line(self, c)) {
                reverse_pos(self);
                state = stop_comment;
            }
        } break;

        case stop_comment:
            assert(self->pos >= start_capture);
            replace_token_sn(self->strings, &res, tok_comment, self->input.v + start_capture,
                             self->pos - start_capture);
            state = stop;
            break;

        case start_hash_command:
            start_capture = self->pos;
            state         = in_hash_command;
            break;

        case in_hash_command: {
            if (self->pos == end) {
                state = stop_hash_command;
                continue;
            }
            // check for #ifc ... #endc block
            size_t const pos = self->pos;
            if ('i' == peek_char(self, pos) && 'f' == peek_char(self, pos + 1) &&
                'c' == peek_char(self, pos + 2)) {
                state = in_ifc;
                start_capture += 3;
                advance_pos_n(self, 3);
                continue;
            }

            char const c = next_char(self);
            if (is_end_of_line(self, c)) {
                reverse_pos(self);
                state = stop_hash_command;
                continue;
            }

        } break;

        case in_ifc: {
            if (self->pos == end) {
                state = stop_hash_command;
                continue;
            }
            size_t const pos = self->pos;
            if ('#' == peek_char(self, pos) && 'e' == peek_char(self, pos + 1) &&
                'n' == peek_char(self, pos + 2) && 'd' == peek_char(self, pos + 3) &&
                'c' == peek_char(self, pos + 4)) {

                assert(self->pos >= start_capture);
                replace_token_sn(self->strings, &res, tok_c_block, self->input.v + start_capture,
                                 self->pos - start_capture);
                advance_pos_n(self, 5);
                state = stop;
                break;
            }
            (void)next_char(self);

        } break;

        case stop_hash_command: {

            assert(self->pos >= start_capture);
            size_t      len       = self->pos - start_capture;
            char const *str_start = self->input.v + start_capture;
            str         command   = str_init_n(self->transient, str_start, len);

            str_array   words     = {.alloc = self->transient};
            str_parse_words(command, &words);

            // Conditional compilation: #ifdef/#ifndef/#endif track nesting via skip_depth.
            // When skip_depth > 0, tokens are skipped. #ifdef/#ifndef always increment
            // skip_depth when already skipping (to match #endif pairs), but #define/#undef
            // are only processed at depth 0.
            if (words.size == 2) {
                if (0 == skip_depth && str_eq(words.v[0], S("define"))) {
                    str_hset_insert(&self->defines, words.v[1]);
                } else if (0 == skip_depth && str_eq(words.v[0], S("undef"))) {
                    str_hset_remove(self->defines, words.v[1]);
                }

                else if (str_eq(words.v[0], S("ifdef"))) {
                    if (skip_depth || !str_hset_contains(self->defines, words.v[1])) skip_depth += 1;
                } else if (str_eq(words.v[0], S("ifndef"))) {
                    if (skip_depth || str_hset_contains(self->defines, words.v[1])) skip_depth += 1;
                } else {
                    goto other_hash;
                }

                state = start;
                goto conditional_hash;
            } else if (words.size == 1 && str_eq(words.v[0], S("endif"))) {
                if (skip_depth) skip_depth -= 1;

                state = start;
                goto conditional_hash;
            }

        other_hash:
            str_deinit(self->transient, &command);
            replace_token_sn(self->strings, &res, tok_hash_command, str_start, len);
            state = stop;
            continue;

        conditional_hash:
            str_deinit(self->transient, &command);
            continue;

        } break;

        case start_char: {
            self->buf.size = 0;
            state          = in_char;
        } break;

        case in_char: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = next_char(self);
            switch (c) {
            case '\\': state = in_char_backslash; break;
            case '\'': state = stop_char; break;
            default:   {
                if (self->buf.size >= 1) {
                    // full
                    tok_error(self, out_err, tl_err_invalid_token);
                    return 1;
                }
                array_push(self->buf, c);
            } break;
            }
        } break;

        case in_char_backslash: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = next_char(self);

            // keep it literal
            char backslash = '\\';
            array_push(self->buf, backslash);
            array_push(self->buf, c);

            state = in_char;

        } break;

        case stop_char: {
            replace_token_sn(self->strings, &res, tok_char, self->buf.v, self->buf.size);
            state = stop;
        } break;

        case start_string: {
            self->buf.size = 0;
            state          = in_string;
        } break;

        case in_string: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = next_char(self);
            switch (c) {
            case '\\': state = in_string_backslash; break;
            case '"':  state = stop_string; break;
            default:   {
                array_push(self->buf, c);
            } break;
            }
        } break;

        case in_string_backslash: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = next_char(self);

            // keep it literal
            char backslash = '\\';
            array_push(self->buf, backslash);
            array_push(self->buf, c);

            state = in_string;

        } break;

        case in_string_backslash_unescaped: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = next_char(self);

            // https://en.cppreference.com/w/cpp/language/escape.html
            // TODO: numeric escapes, octal, hex
            char actual = 0;
            switch (c) {
            case '\'': actual = 0x27; break;
            case '"':  actual = 0x22; break;
            case '?':  actual = 0x3f; break;
            case '\\': actual = 0x5c; break;
            case 'a':  actual = 0x07; break;
            case 'b':  actual = 0x08; break;
            case 'f':  actual = 0x0c; break;
            case 'n':  actual = 0x0a; break;
            case 'r':  actual = 0x0d; break;
            case 't':  actual = 0x09; break;
            case 'v':  actual = 0x0b; break;
            default:   break;
            }
            if (actual) {
                array_push(self->buf, actual);
            } else {
                // unrecognised escape sequence, keep it literal
                char backslash = '\\';
                array_push(self->buf, backslash);
                array_push(self->buf, c);
            }
            state = in_string;

        } break;

        case stop_string: {
            replace_token_sn(self->strings, &res, string_format == str_s_prefix ? tok_s_string : tok_string,
                             self->buf.v, self->buf.size);
            string_format = str_default;
            state         = stop;
        } break;

        case error:
        case stop:  goto finish; break;
        }
    }

finish:

    if (start == state) {
        if (out_err) tok_error(self, out_err, tl_err_eof);
        return 1;
    }

    else if (stop == state) {
        if (0 != skip_depth) goto start;

        res.file = alloc_strdup(self->parent, str_cstr(&self->file));
        res.line = self->line;
        res.col  = self->col;
        alloc_copy(out, &res);
        return 0;
    } else if (error == state) {
        return 1;
    }

    else {
        assert(0);
    }

    return 1;
}

// -- backtracking --

void tokenizer_put_back(tokenizer *self, token const *toks, size_t n_toks) {

    for (size_t i = n_toks; i != 0; --i) {
        // dbg("tokenizer put back: %s\n", token_to_string(self->strings, &toks[i - 1]));
        array_push(self->backtrack, toks[i - 1]);
    }
}

void tokenizer_set_file(tokenizer *self, str file) {
    self->file = str_copy(self->parent, file);
    self->line = TOKENIZER_LINE_START;
}
