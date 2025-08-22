#include "sexp_parser.h"
#include "alloc.h"

#include <assert.h>
#include <stdbool.h>

static const size_t TOKENIZER_BUF_SIZE = 1024;

int sexp_tokenizer_init(allocator *alloc, sexp_tokenizer *t, char const *input, size_t len) {
  alloc_zero(t);

  t->alloc     = alloc;
  t->input     = input;
  t->input_len = len;

  t->buf       = alloc->malloc(alloc, TOKENIZER_BUF_SIZE);
  t->buf_len   = 0;

  if (NULL == t->buf) return 1;
  return 0;
}

void sexp_tokenizer_deinit(sexp_tokenizer *t) {
  t->alloc->free(t->alloc, t->buf);
  alloc_invalidate(t);
}

int sexp_tokenizer_next(sexp_tokenizer *self, sexp_token *out, sexp_tokenizer_err_tag *err,
                        size_t *err_pos) {
  // state machine
  enum {
    start,

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
    error
  } state          = start;

  size_t const end = self->input_len;

  // starting position for number or symbol
  size_t start_capture = 0;

  while (true) {

    switch (state) {

    case start: {
      if (self->pos == end) goto finish;

      char const c = self->input[self->pos++];

      switch (c) {
      case '+': // FIXME ' + ' should not return number
      case '-':
        --self->pos;
        state = start_number_sign;
        continue;

      case '"': state = start_string; continue;

      case ';': state = start_comment; break;

      case '(':
        sexp_token_init(out, sexp_tok_open_round);
        state = stop;
        break;

      case ')':
        sexp_token_init(out, sexp_tok_close_round);
        state = stop;
        break;

      case '\'':
        sexp_token_init(out, sexp_tok_single_quote);
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
      case '\'':
      case '"':
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
      if (sexp_token_init_str(self->alloc, out, sexp_tok_symbol, self->input + start_capture,
                              self->pos - start_capture)) {
        *err     = sexp_tok_err_oom;
        *err_pos = self->pos;
        return 1;
      }
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
      if (sexp_token_init_str(self->alloc, out, sexp_tok_comment, self->input + start_capture,
                              self->pos - start_capture)) {
        *err     = sexp_tok_err_oom;
        *err_pos = self->pos;
        return 1;
      }
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
          *err     = sexp_tok_err_invalid_token;
          *err_pos = self->pos;
          state    = error;
        }
        break;
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
      if (sexp_token_init_str(self->alloc, out, sexp_tok_number, self->input + start_capture,
                              self->pos - start_capture)) {
        *err     = sexp_tok_err_oom;
        *err_pos = self->pos;
        return 1;
      }
      state = stop;
    } break;

    case start_string: {
      self->buf_len = 0;
      state         = in_string;
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
      default:   self->buf[self->buf_len++] = c; break;
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
        self->buf[self->buf_len++] = actual;

      } else {
        // unrecognised escape sequence, keep it literal
        self->buf[self->buf_len++] = '\\';
        self->buf[self->buf_len++] = c;
      }
      state = in_string;

    } break;

    case stop_string: {
      if (sexp_token_init_str(self->alloc, out, sexp_tok_string, self->buf, self->buf_len)) {
        *err     = sexp_tok_err_oom;
        *err_pos = self->pos;
        return 1;
      }
      state = stop;
    } break;

    case stop:
    case error: goto finish; break;
    }
  }

finish:

  if (start == state) {
    *err     = sexp_tok_err_eof;
    *err_pos = self->pos;
    return 1;
  } else if (stop == state) {
    return 0;
  } else if (error == state) {
    return 1;
  } else {
    assert(false);
    *err     = sexp_tok_err_unexpected_error;
    *err_pos = self->pos;
    return 1;
  }
}

// -- token --
void sexp_token_init(sexp_token *self, sexp_token_tag tag) {
  self->s   = NULL;
  self->tag = tag;
}

nodiscard int sexp_token_init_str(allocator *alloc, sexp_token *self, sexp_token_tag tag, char const *src,
                                  size_t len) {

  self->s = alloc_strndup(alloc, src, len);
  if (NULL == self->s) return 1;

  self->tag = tag;

  return 0;
}

void sexp_token_deinit(allocator *alloc, sexp_token *self) {
  switch (self->tag) {
  case sexp_tok_open_round:
  case sexp_tok_close_round:
  case sexp_tok_single_quote: break;
  case sexp_tok_number:
  case sexp_tok_string:
  case sexp_tok_symbol:
  case sexp_tok_comment:      alloc->free(alloc, self->s); break;
  }

  alloc_invalidate(self);
}
