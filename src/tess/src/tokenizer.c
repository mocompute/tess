#include "tokenizer.h"

#include "alloc.h"
#include "array.h"
#include "token.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct tokenizer {
    allocator  *parent;
    allocator  *strings;
    char_cslice input;
    char const *file;
    u32         line;
    u32         pos;

    token_array backtrack;
    char_array  buf;
};

// -- statics --

static void tok_error(tokenizer_error *err, tess_error_tag tag, char const *file, u32 line) {
    err->tag  = tag;
    err->file = file;
    err->line = line;
}

// -- allocation and deallocation --

tokenizer *tokenizer_create(allocator *alloc, char_cslice input, char const *file) {
    tokenizer *self = alloc_calloc(alloc, 1, sizeof(tokenizer));

    self->parent    = alloc;
    self->strings   = arena_create(alloc, 4096);
    self->input     = input;
    self->pos       = input.begin;
    self->file      = file;
    self->line      = 0;

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

static void replace_token_v(allocator *alloc, token *tok, token_tag tag, u8 val) {
    token_deinit(alloc, tok);
    token_init_v(tok, tag, val);
}

static void replace_token_s(allocator *alloc, token *tok, token_tag tag, char const *s) {
    token_deinit(alloc, tok);
    token_init_s(alloc, tok, tag, s);
}

static void replace_token_sn(allocator *alloc, token *tok, token_tag tag, char const *s, size_t len) {
    token_deinit(alloc, tok);
    token_init_sn(alloc, tok, tag, s, len);
}

int tokenizer_next(tokenizer *self, token *out, tokenizer_error *out_err) {
    assert(out);

    // support backtracking by parser
    if (self->backtrack.size) {
        *out = self->backtrack.v[--self->backtrack.size];
        return 0;
    }

    // state machine
    enum {
        start,

        in_newline,
        in_newline_indent,
        stop_newline_indent,

        in_minus,

        in_equal,

        in_colon,

        forward_slash,

        start_number,
        start_number_sign,
        in_number,
        in_number_sign,
        stop_number,

        start_string,
        in_string,
        in_string_backslash,
        stop_string,

        start_symbol,
        in_symbol,
        stop_symbol,

        start_comment,
        in_comment,
        stop_comment,

        stop,
    } state          = start;

    size_t const end = self->input.end;

    // starting position for number or symbol or indent
    size_t start_capture = 0;

    // return value, to be copied to *out
    token res = {.file = self->file};

    while (true) {

        switch (state) {

        case start: {
            if (self->pos >= end) {
                tok_error(out_err, tess_err_eof, self->file, self->line);
                goto finish;
            }

            char const c = self->input.v[self->pos++];

            switch (c) {
            case '=': state = in_equal; break;

            case '-': state = in_minus; break;

            case '+':
                --self->pos;
                state = start_number_sign;
                continue;

            case '"': state = start_string; continue;

            case '\n':
                state = in_newline;
                self->line++;
                continue;

            case '/': state = forward_slash; continue;

            case '.':
                replace_token(self->strings, &res, tok_dot);
                state = stop;
                break;

            case ':': state = in_colon; continue;

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

            case ')':
                replace_token(self->strings, &res, tok_close_round);
                state = stop;
                break;

            default:
                if (c >= '0' && c <= '9') {
                    // any digit starts a number
                    --self->pos;
                    state = start_number;
                    continue;
                }

                if (c > 0x20 && c < 0x7f) {
                    // any other printable character starts a symbol
                    --self->pos;
                    state = start_symbol;
                    continue;
                }

                // else skip char
                break;
            }
        } break;

        case in_colon: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_colon);
                state = stop;
                goto finish;
            }
            char const c = self->input.v[self->pos++];
            switch (c) {
            case '=':
                replace_token(self->strings, &res, tok_colon_equal);
                state = stop;
                break;
            default:
                self->pos -= 1;
                replace_token(self->strings, &res, tok_colon);
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
            char const c = self->input.v[self->pos++];
            switch (c) {
            case '>':
                replace_token(self->strings, &res, tok_arrow);
                state = stop;
                break;
            default:
                self->pos -= 2;
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
            char const c = self->input.v[self->pos++];
            if (' ' == c || '\n' == c) {
                replace_token(self->strings, &res, tok_equal_sign);
                state = stop;
                if ('\n' == c) self->line++;
                goto finish;
            }

            self->pos -= 2;
            state = start_symbol;

        } break;

        case forward_slash: {
            if (self->pos == end) {
                replace_token_s(self->strings, &res, tok_symbol, "/");
                state = stop;
                goto finish;
            }

            char const c = self->input.v[self->pos++];
            switch (c) {
            case '/': state = start_comment; continue;
            default:
                self->pos -= 2;
                state = start_symbol;
                break;
            }

        } break;

        case in_newline: {
            if (self->pos == end) {
                replace_token(self->strings, &res, tok_one_newline);
                state = stop;
                goto finish;
            }

            char const c = self->input.v[self->pos++];

            switch (c) {
            case '\n':
                replace_token(self->strings, &res, tok_two_newline);
                state = stop;
                self->line++;
                break;
            case ' ':
                start_capture = self->pos - 1;
                state         = in_newline_indent;
                continue; // TODO tab indent

            default:
                replace_token(self->strings, &res, tok_one_newline);
                --self->pos;
                state = stop;
                break;
            }

        } break;

        case in_newline_indent: {
            if (self->pos == end) {
                state = stop_newline_indent;
                continue;
            }

            char const c = self->input.v[self->pos++];

            switch (c) {
            case ' ': continue;
            default:
                --self->pos;
                state = stop_newline_indent;
                continue;
            }

        } break;

        case stop_newline_indent:
            if (self->pos - start_capture > 0xff) {
                if (out_err) tok_error(out_err, tess_err_indent_too_long, self->file, self->line);
                return 1;
            }

            replace_token_v(self->strings, &res, tok_newline_indent, (u8)(self->pos - start_capture));
            state = stop;
            break;

        case start_number: {
            start_capture = self->pos;
            state         = in_number;
        } break;

        case start_number_sign: {
            start_capture = self->pos;
            ++self->pos; // capture the +/-
            state = in_number_sign;
        } break;

        case in_number: {
            if (self->pos == end) {
                state = stop_number;
                continue;
            }

            char const c = self->input.v[self->pos++];

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
            case ' ':
            case ')':
            case ',':
            case ':':
            case ';':
                --self->pos;
                state = stop_number;
                break;

            default:
                // all invisible and extended characters break a symbol
                if (c < 0x20) { // c is signed so this catches c > 0x7f
                    --self->pos;
                    state = stop_number;
                } else {
                    --self->pos;
                    if (out_err) tok_error(out_err, tess_err_invalid_token, self->file, self->line);
                    return 1;
                }
            }
        } break;

        case in_number_sign: {
            if (self->pos == end) {
                state = stop_symbol;
                continue;
            }

            // + or - not start of a number
            char const c = self->input.v[self->pos];
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

            char const c = self->input.v[self->pos++];
            switch (c) {
            case '(':
            case ')':
            case ' ':
            case '"':
            case ':':
            case '=':
            case ';':
            case ',':
            case '.':
                // these tokens break a symbol TODO there should be more
                --self->pos;
                state = stop_symbol;
                break;
            default:
                // all invisible and extended characters break a symbol
                if (c < 0x20) { // c is signed so this catches c > 0x7f
                    --self->pos;
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
            i8 const c = self->input.v[self->pos++];
            if (c < 0x20) { // c is signed so this catches c > 0x7f
                --self->pos;
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
            char const c = self->input.v[self->pos++];
            switch (c) {
            case '\\': state = in_string_backslash; break;
            case '"':  state = stop_string; break;
            default:   {
                array_push(self->buf, &c);
            } break;
            }
        } break;

        case in_string_backslash: {
            if (self->pos == end) {
                state = stop_string;
                continue;
            }
            char const c = self->input.v[self->pos++];

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
                array_push(self->buf, &actual);
            } else {
                // unrecognised escape sequence, keep it literal
                char backslash = '\\';
                array_push(self->buf, &backslash);
                array_push(self->buf, &c);
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
        if (out_err) tok_error(out_err, tess_err_eof, self->file, self->line);
        return 1;
    }

    else if (stop == state) {
        res.line = self->line;
        alloc_copy(out, &res);
        return 0;

    } else {
        assert(false);
    }

    return 0;
}

// -- backtracking --

void tokenizer_put_back(tokenizer *self, token const *toks, size_t n_toks) {

    for (size_t i = n_toks; i != 0; --i) {
        array_push(self->backtrack, &toks[i - 1]);
    }
}
