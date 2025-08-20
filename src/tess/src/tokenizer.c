#include "tokenizer.h"

#include "alloc.h"
#include "token.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct tess_tokenizer {
  char const       *input;
  size_t            input_len;
  size_t            pos;

  struct mos_vector buf;
  struct mos_vector backtrack;
};

// -- statics --

static void tok_error(tess_tokenizer_error_t *err, tess_error_tag_t tag, size_t pos) {
  err->tag = tag;
  err->pos = pos;
}

// -- allocation and deallocation --

tess_tokenizer_t *tess_tokenizer_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(tess_tokenizer_t));
}

void tess_tokenizer_dealloc(mos_allocator_t *alloc, tess_tokenizer_t **tok) {
  alloc->free(*tok);
  *tok = 0;
}

void tess_tokenizer_init(mos_allocator_t *alloc, tess_tokenizer_t *tok, char const *input, size_t len) {
  tok->input     = input;
  tok->input_len = len;
  tok->pos       = 0;

  mos_vector_init(&tok->buf, sizeof(char));
  mos_vector_reserve(alloc, &tok->buf, 32);
  mos_vector_init(&tok->backtrack, sizeof(tess_token_t));
  mos_vector_reserve(alloc, &tok->backtrack, 8);
}

void tess_tokenizer_deinit(mos_allocator_t *alloc, tess_tokenizer_t *tok) {
  mos_vector_deinit(alloc, &tok->backtrack);
  mos_vector_deinit(alloc, &tok->buf);
  mos_alloc_invalidate(tok, sizeof *tok);
}

void tess_tokenizer_error_init(tess_tokenizer_error_t *err) {
  // future
  (void)err;
}

void tess_tokenizer_error_deinit(tess_tokenizer_error_t *err) {
  // future
  (void)err;
}

// -- parsing --

void replace_token(mos_allocator_t *alloc, tess_token_t *tok, tess_token_tag_t tag) {
  tess_token_deinit(alloc, tok);
  tess_token_init(tok, tag);
}

void replace_token_v(mos_allocator_t *alloc, tess_token_t *tok, tess_token_tag_t tag, uint8_t val) {
  tess_token_deinit(alloc, tok);
  tess_token_init_v(tok, tag, val);
}

void replace_token_s(mos_allocator_t *alloc, tess_token_t *tok, tess_token_tag_t tag, char const *s) {
  tess_token_deinit(alloc, tok);
  tess_token_init_s(alloc, tok, tag, s);
}

void replace_token_sn(mos_allocator_t *alloc, tess_token_t *tok, tess_token_tag_t tag, char const *s,
                      size_t len) {
  tess_token_deinit(alloc, tok);
  tess_token_init_sn(alloc, tok, tag, s, len);
}

int tess_tokenizer_next(mos_allocator_t *alloc, tess_tokenizer_t *self, tess_token_t *out,
                        tess_tokenizer_error_t *out_err) {
  assert(out);

  // support backtracking by parser
  if (!mos_vector_empty(&self->backtrack)) {
    memcpy(out, mos_vector_back(&self->backtrack), sizeof *out);
    mos_vector_pop_back(&self->backtrack);
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

  size_t const end = self->input_len;

  // starting position for number or symbol or indent
  size_t start_capture = 0;

  // return value, to be copied to *out
  tess_token_t res = {0};

  while (true) {

    switch (state) {

    case start: {
      if (self->pos == end) goto finish;

      char const c = self->input[self->pos++];

      switch (c) {
      case '=': state = in_equal; break;

      case '-': state = in_minus; break;

      case '+':
        --self->pos;
        state = start_number_sign;
        continue;

      case '"':  state = start_string; continue;

      case '\n': state = in_newline; continue;

      case '/':  state = forward_slash; continue;

      case ';':
        replace_token(alloc, &res, tess_tok_semicolon);
        state = stop;
        break;

      case ',':
        replace_token(alloc, &res, tess_tok_comma);
        state = stop;
        break;

      case '(':
        replace_token(alloc, &res, tess_tok_open_round);
        state = stop;
        break;

      case ')':
        replace_token(alloc, &res, tess_tok_close_round);
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

    case in_minus: {
      if (self->pos == end) {
        replace_token_s(alloc, &res, tess_tok_symbol, "-");
        state = stop;
        goto finish;
      }
      char const c = self->input[self->pos++];
      switch (c) {
      case '>':
        replace_token(alloc, &res, tess_tok_arrow);
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
        replace_token(alloc, &res, tess_tok_equal_sign);
        state = stop;
        goto finish;
      }
      char const c = self->input[self->pos++];
      if (' ' == c || '\n' == c) {
        replace_token(alloc, &res, tess_tok_equal_sign);
        state = stop;
        goto finish;
      }

      self->pos -= 2;
      state = start_symbol;

    } break;

    case forward_slash: {
      if (self->pos == end) {
        replace_token_s(alloc, &res, tess_tok_symbol, "/");
        state = stop;
        goto finish;
      }

      char const c = self->input[self->pos++];
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
        replace_token(alloc, &res, tess_tok_one_newline);
        state = stop;
        goto finish;
      }

      char const c = self->input[self->pos++];

      switch (c) {
      case '\n':
        replace_token(alloc, &res, tess_tok_two_newline);
        state = stop;
        break;
      case ' ':
        start_capture = self->pos - 1;
        state         = in_newline_indent;
        continue; // TODO tab indent

      default:
        replace_token(alloc, &res, tess_tok_one_newline);
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

      char const c = self->input[self->pos++];

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
        if (out_err) tok_error(out_err, tess_err_indent_too_long, self->pos);
        return 1;
      }

      replace_token_v(alloc, &res, tess_tok_newline_indent, (uint8_t)(self->pos - start_capture));
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

      char const c = self->input[self->pos++];

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
          if (out_err) tok_error(out_err, tess_err_invalid_token, self->pos);
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
      char const c = self->input[self->pos];
      switch (c) {
      case ' ':
      case ')': state = stop_symbol; break;
      default:  state = in_number; break;
      }
    } break;

    case stop_number: {
      assert(self->pos >= start_capture);
      replace_token_sn(alloc, &res, tess_tok_number, self->input + start_capture,
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

      char const c = self->input[self->pos++];
      switch (c) {
      case '(':
      case ')':
      case ' ':
      case '"':
      case ';':
      case ',':
        // these tokens break a symbol
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
      replace_token_sn(alloc, &res, tess_tok_symbol, self->input + start_capture,
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
      char const c = self->input[self->pos++];
      if (c < 0x20) { // c is signed so this catches c > 0x7f
        --self->pos;
        state = stop_comment;
      }
    } break;

    case stop_comment:
      assert(self->pos >= start_capture);
      replace_token_sn(alloc, &res, tess_tok_comment, self->input + start_capture,
                       self->pos - start_capture);
      state = stop;
      break;

    case start_string: {
      mos_vector_clear(&self->buf);
      state = in_string;
    } break;

    case in_string: {
      if (self->pos == end) {
        state = stop_string;
        continue;
      }
      char const c = self->input[self->pos++];
      switch (c) {
      case '\\': state = in_string_backslash; break;
      case '"':  state = stop_string; break;
      default:
        if (mos_vector_push_back(alloc, &self->buf, &c)) {
          if (out_err) tok_error(out_err, tess_err_out_of_memory, self->pos);
          return 1;
        }

        break;
      }
    } break;

    case in_string_backslash: {
      if (self->pos == end) {
        state = stop_string;
        continue;
      }
      char const c = self->input[self->pos++];

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
        if (mos_vector_push_back(alloc, &self->buf, &actual)) {
          if (out_err) tok_error(out_err, tess_err_out_of_memory, self->pos);
          return 1;
        }

      } else {
        // unrecognised escape sequence, keep it literal
        char backslash = '\\';
        if (mos_vector_push_back(alloc, &self->buf, &backslash) ||
            mos_vector_push_back(alloc, &self->buf, &c)) {
          if (out_err) tok_error(out_err, tess_err_out_of_memory, self->pos);
          return 1;
        }
      }
      state = in_string;

    } break;

    case stop_string: {
      replace_token_sn(alloc, &res, tess_tok_string, mos_vector_data(&self->buf),
                       mos_vector_size(&self->buf));
      state = stop;
    } break;

    case stop: goto finish; break;
    }
  }

finish:

  if (start == state) {
    if (out_err) tok_error(out_err, tess_err_eof, self->pos);
    return 1;
  }

  else if (stop == state) {
    memcpy(out, &res, sizeof *out);
    return 0;

  } else {
    assert(false);
  }

  return 0;
}

// -- backtracking --

void tess_tokenizer_put_back(mos_allocator_t *alloc, tess_tokenizer_t *self, tess_token_t const *toks,
                             size_t n_toks) {
  for (size_t i = n_toks; i != 0; --i) {
    mos_vector_push_back(alloc, &self->backtrack, &toks[i - 1]);
  }
}
