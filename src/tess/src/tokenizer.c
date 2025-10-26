#include "tokenizer.h"

#include "alloc.h"
#include "array.h"
#include "dbg.h"
#include "token.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct tokenizer {
    allocator  *parent;
    allocator  *strings;
    char_csized input;
    char const *file;
    u32         line;
    u32         pos;
    u32         col;

    token_array backtrack;
    char_array  buf;
};

// -- statics --

static void tok_error(tokenizer *self, tokenizer_error *err, tl_error_tag tag) {
    err->tag  = tag;
    err->file = self->file;
    err->line = self->line;
    err->col  = self->col;
}

// -- allocation and deallocation --

tokenizer *tokenizer_create(allocator *alloc, char_csized input, char const *file) {
    tokenizer *self = alloc_calloc(alloc, 1, sizeof(tokenizer));

    self->parent    = alloc;
    self->strings   = arena_create(alloc, 4096);
    self->input     = input;
    self->pos       = 0;
    self->file      = file;
    self->line      = 1;
    self->col       = 0;

    self->buf       = (char_array){.alloc = alloc};
    self->backtrack = (token_array){.alloc = alloc};
    array_reserve(self->buf, 32);
    array_reserve(self->backtrack, 8);

    return self;
}

void tokenizer_destroy(tokenizer **self) {
    arena_destroy((*self)->parent, &(*self)->strings);

    array_free((*self)->backtrack);
    array_free((*self)->buf);

    alloc_free((*self)->parent, *self);
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

static void advance_line(tokenizer *self) {
    self->line++;
    self->col = 0;
}

static void advance_pos(tokenizer *self) {
    self->col++;
    self->pos++;
}

static void reverse_pos(tokenizer *self) {
    self->col--;
    self->pos--;
}

static char next_char(tokenizer *self) {
    self->col++;
    return self->input.v[self->pos++];
}

int tokenizer_next(tokenizer *self, token *out, tokenizer_error *out_err) {
    assert(out);
    out_err->tag = tl_err_ok;

    // support backtracking by parser
    if (self->backtrack.size) {
        *out = self->backtrack.v[--self->backtrack.size];
        return 0;
    }

    // state machine
    enum {
        start,

        in_bang,
        in_minus,
        in_equal,
        in_colon,
        in_dot,
        in_dot_2,
        in_ampersand,

        forward_slash,

        start_number,
        start_number_sign,
        in_number,
        in_number_sign,
        stop_number,

        start_string,
        in_string,
        in_string_backslash,
        in_string_backslash_unescaped,
        stop_string,

        start_symbol,
        in_symbol,
        stop_symbol,

        start_comment,
        in_comment,
        stop_comment,

        stop,
    } state          = start;

    size_t const end = self->input.size;

    // starting position for number or symbol or indent
    size_t start_capture = 0;

    // return value, to be copied to *out
    token res = {.file = self->file};

    while (1) {

        switch (state) {

        case start: {
            if (self->pos >= end) {
                tok_error(self, out_err, tl_err_eof);
                goto finish;
            }

            char const c = next_char(self);

            switch (c) {
            case '=': state = in_equal; break;
            case '&': state = in_ampersand; break;
            case '-': state = in_minus; break;

            case '+':
                reverse_pos(self);
                state = start_number_sign;
                continue;

            case '"':  state = start_string; continue;

            case '\n': advance_line(self); continue;

            case '/':  state = forward_slash; continue;
            case '.':  state = in_dot; continue;
            case ':':  state = in_colon; continue;
            case '!':  state = in_bang; continue;

            case ';':
                replace_token(self->strings, &res, tok_semicolon);
                state = stop;
                break;

            case ',':
                replace_token(self->strings, &res, tok_comma);
                state = stop;
                break;

            case '*':
                replace_token(self->strings, &res, tok_star);
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

            case '[':
                replace_token(self->strings, &res, tok_open_square);
                state = stop;
                break;

            case ')':
                replace_token(self->strings, &res, tok_close_round);
                state = stop;
                break;

            case '}':
                replace_token(self->strings, &res, tok_close_curly);
                state = stop;
                break;

            case ']':
                replace_token(self->strings, &res, tok_close_square);
                state = stop;
                break;

            default:
                if (c >= '0' && c <= '9') {
                    // any digit starts a number
                    reverse_pos(self);
                    state = start_number;
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
            default:
                reverse_pos(self);
                replace_token(self->strings, &res, tok_colon);
                state = stop;
                break;
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
            default:
                reverse_pos(self);
                reverse_pos(self); // minus 2
                state = start_number_sign;
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
            if (' ' == c || '\n' == c) {
                replace_token(self->strings, &res, tok_equal_sign);
                state = stop;
                if ('\n' == c) advance_line(self);
                goto finish;
            } else if ('=' == c) {
                // ==
                replace_token(self->strings, &res, tok_equal_equal);
                state = stop;
                goto finish;
            }

            reverse_pos(self);
            reverse_pos(self); // minus 2
            state = start_symbol;

        } break;

        case in_ampersand: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_ampersand);
                state = stop;
                goto finish;
            }
            char const c = next_char(self);
            if (' ' == c || '\n' == c) {
                replace_token(self->strings, &res, tok_ampersand);
                state = stop;
                if ('\n' == c) advance_line(self);
                goto finish;
            } else if ('&' == c) {
                // &&
                replace_token(self->strings, &res, tok_logical_and);
                state = stop;
                goto finish;
            }

            reverse_pos(self);
            reverse_pos(self); // minus 2
            state = start_symbol;

        } break;

        case forward_slash: {
            if (self->pos == end) {
                replace_token_s(self->strings, &res, tok_symbol, "/");
                state = stop;
                goto finish;
            }

            char const c = next_char(self);
            switch (c) {
            case '/': state = start_comment; continue;
            default:
                reverse_pos(self);
                reverse_pos(self); // minus 2
                state = start_symbol;
                break;
            }

        } break;

        case start_number: {
            start_capture = self->pos;
            state         = in_number;
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

            if (c >= '0' && c <= '9') continue;
            switch (c) {
            case 'e':
            case 'E':
            case 'p':
            case 'P':
            case 'x':
            case 'X':
            case '+':
            case '-':
            case '.':
            case '_': continue;

            default:
                // all other characters break a symbol
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

        case stop_number: {
            assert(self->pos >= start_capture);
            replace_token_sn(self->strings, &res, tok_number, self->input.v + start_capture,
                             self->pos - start_capture);
            state = stop;
        } break;

        case start_symbol: {
            start_capture = self->pos;
            state         = in_symbol;
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
            if (c == '\n') {
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
            replace_token_sn(self->strings, &res, tok_string, self->buf.v, self->buf.size);
            state = stop;
        } break;

        case stop: goto finish; break;
        }
    }

finish:

    if (start == state) {
        if (out_err) tok_error(self, out_err, tl_err_eof);
        return 1;
    }

    else if (stop == state) {
        res.file = self->file;
        res.line = self->line;
        res.col  = self->col;
        alloc_copy(out, &res);
        return 0;

    } else {
        assert(0);
    }

    return 0;
}

// -- backtracking --

void tokenizer_put_back(tokenizer *self, token const *toks, size_t n_toks) {

    for (size_t i = n_toks; i != 0; --i) {
        // dbg("tokenizer put back: %s\n", token_to_string(self->strings, &toks[i - 1]));
        array_push(self->backtrack, toks[i - 1]);
    }
}
