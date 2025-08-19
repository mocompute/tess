#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct tess_tokenizer_t {
  char const  *input;
  size_t       input_len;
  size_t       pos;

  mos_vector_t buf;
  mos_vector_t backtrack;
};

// -- statics --

static void tok_error(tess_tokenizer_error_t *err, tess_tokenizer_error_tag_t tag, size_t pos) {
  err->tag = tag;
  err->pos = pos;
}

//

tess_tokenizer_t *tess_tokenizer_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(tess_tokenizer_t));
}

void tess_tokenizer_dealloc(mos_allocator_t *alloc, tess_tokenizer_t *tok) {
  alloc->free(tok);
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
}

void tess_tokenizer_put_back(mos_allocator_t *alloc, tess_tokenizer_t *tokenizer, tess_token_t const *toks,
                             size_t n_toks) {
  for (size_t i = n_toks; i != 0; --i) {
    mos_vector_push_back(alloc, &tokenizer->backtrack, &toks[i - 1]);
  }
}

//

int tess_tokenizer_next(mos_allocator_t *alloc, tess_tokenizer_t *tokenizer, tess_token_t *out,
                        tess_tokenizer_error_t *out_err) {
  assert(out);

  // support backtracking by parser
  if (!mos_vector_empty(&tokenizer->backtrack)) {
    *out = *(tess_token_t *)mos_vector_back(&tokenizer->backtrack);
    mos_vector_pop_back(&tokenizer->backtrack);
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

  size_t const end = tokenizer->input_len;

  // starting position for number or symbol or indent
  size_t start_capture = 0;

  // return value, to be copied to *out
  tess_token_t res = {0};

  while (true) {

    switch (state) {

    case start: {
      if (tokenizer->pos == end) goto finish;

      char const c = tokenizer->input[tokenizer->pos++];

      switch (c) {
      case '=': state = in_equal; break;

      case '-': state = in_minus; break;

      case '+':
        --tokenizer->pos;
        state = start_number_sign;
        continue;

      case '"':  state = start_string; continue;

      case '\n': state = in_newline; continue;

      case '/':  state = forward_slash; continue;

      case ';':
        res.tag = semicolon;
        state   = stop;
        break;

      case ',':
        res.tag = comma;
        state   = stop;
        break;

      case '(':
        res.tag = open_round;
        state   = stop;
        break;

      case ')':
        res.tag = close_round;
        state   = stop;
        break;

      default:
        if (c >= '0' && c <= '9') {
          // any digit starts a number
          --tokenizer->pos;
          state = start_number;
          continue;
        }

        if (c > 0x20 && c < 0x7f) {
          // any other printable character starts a symbol
          --tokenizer->pos;
          state = start_symbol;
          continue;
        }

        // else skip char
        break;
      }
    } break;

    case in_minus: {
      if (tokenizer->pos == end) {
        res.tag = symbol;
        res.s   = "-";
        state   = stop;
        goto finish;
      }
      char const c = tokenizer->input[tokenizer->pos++];
      switch (c) {
      case '>':
        res.tag = arrow;
        state   = stop;
        break;
      default:
        tokenizer->pos -= 2;
        state = start_number_sign;
        break;
      }
    } break;

    case in_equal: {
      if (tokenizer->pos == end) {
        res.tag = equal_sign;
        state   = stop;
        goto finish;
      }
      char const c = tokenizer->input[tokenizer->pos++];
      if (' ' == c || '\n' == c) {
        res.tag = equal_sign;
        state   = stop;
        goto finish;
      }

      tokenizer->pos -= 2;
      state = start_symbol;

    } break;

    case forward_slash: {
      if (tokenizer->pos == end) {
        res.tag = symbol;
        res.s   = "/";
        state   = stop;
        goto finish;
      }

      char const c = tokenizer->input[tokenizer->pos++];
      switch (c) {
      case '/': state = start_comment; continue;
      default:
        tokenizer->pos -= 2;
        state = start_symbol;
        break;
      }

    } break;

    case in_newline: {
      if (tokenizer->pos == end) {
        res.tag = one_newline;
        state   = stop;
        goto finish;
      }

      char const c = tokenizer->input[tokenizer->pos++];

      switch (c) {
      case '\n':
        res.tag = two_newline;
        state   = stop;
        break;
      case ' ':
        start_capture = tokenizer->pos - 1;
        state         = in_newline_indent;
        continue; // TODO tab indent

      default:
        res.tag = one_newline;
        --tokenizer->pos;
        state = stop;
        break;
      }

    } break;

    case in_newline_indent: {
      if (tokenizer->pos == end) {
        state = stop_newline_indent;
        continue;
      }

      char const c = tokenizer->input[tokenizer->pos++];

      switch (c) {
      case ' ': continue;
      default:
        --tokenizer->pos;
        state = stop_newline_indent;
        continue;
      }

    } break;

    case stop_newline_indent:
      if (tokenizer->pos - start_capture > 0xff) {
        if (out_err) tok_error(out_err, indent_too_long, tokenizer->pos);
        return 1;
      }
      // return std::unexpected(tokenizer_error_t(indent_too_long, tokenizer->pos));

      res.tag = newline_indent;
      res.val = (uint8_t)(tokenizer->pos - start_capture);
      state   = stop;
      break;

    case start_number: {
      start_capture = tokenizer->pos;
      state         = in_number;
    } break;

    case start_number_sign: {
      start_capture = tokenizer->pos;
      ++tokenizer->pos; // capture the +/-
      state = in_number_sign;
    } break;

    case in_number: {
      if (tokenizer->pos == end) {
        state = stop_number;
        continue;
      }

      char const c = tokenizer->input[tokenizer->pos++];

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
        --tokenizer->pos;
        state = stop_number;
        break;

      default:
        // all invisible and extended characters break a symbol
        if (c < 0x20) { // c is signed so this catches c > 0x7f
          --tokenizer->pos;
          state = stop_number;
        } else {
          --tokenizer->pos;
          if (out_err) tok_error(out_err, invalid_token, tokenizer->pos);
          return 1;
        }
      }
    } break;

    case in_number_sign: {
      if (tokenizer->pos == end) {
        state = stop_symbol;
        continue;
      }

      // + or - not start of a number
      char const c = tokenizer->input[tokenizer->pos];
      switch (c) {
      case ' ':
      case ')': state = stop_symbol; break;
      default:  state = in_number; break;
      }
    } break;

    case stop_number: {
      assert(tokenizer->pos >= start_capture);
      tess_token_deinit(alloc, &res);
      tess_token_init_sn(alloc, &res, number, tokenizer->input + start_capture,
                         tokenizer->pos - start_capture);
      // res.tag = number;
      // res.s =
      //   std::string{tokenizer->input.data() + start_capture, tokenizer->input.data() + tokenizer->pos};
      state = stop;
    } break;

    case start_symbol: {
      start_capture = tokenizer->pos;
      state         = in_symbol;
    } break;

    case in_symbol: {
      if (tokenizer->pos == end) {
        state = stop_symbol;
        continue;
      }

      char const c = tokenizer->input[tokenizer->pos++];
      switch (c) {
      case '(':
      case ')':
      case ' ':
      case '"':
      case ';':
      case ',':
        // these tokens break a symbol
        --tokenizer->pos;
        state = stop_symbol;
        break;
      default:
        // all invisible and extended characters break a symbol
        if (c < 0x20) { // c is signed so this catches c > 0x7f
          --tokenizer->pos;
          state = stop_symbol;
        }
        break;
      }

    } break;

    case stop_symbol: {
      assert(tokenizer->pos >= start_capture);
      tess_token_deinit(alloc, &res);
      tess_token_init_sn(alloc, &res, symbol, tokenizer->input + start_capture,
                         tokenizer->pos - start_capture);
      // res.tag = symbol;
      // res.s   = {
      //   std::string{tokenizer->input.data() + start_capture, tokenizer->input.data() +
      //   tokenizer->pos}};
      state = stop;

    } break;

    case start_comment:
      start_capture = tokenizer->pos;
      state         = in_comment;
      break;

    case in_comment: {
      if (tokenizer->pos == end) {
        state = stop_comment;
        continue;
      }
      char const c = tokenizer->input[tokenizer->pos++];
      if (c < 0x20) { // c is signed so this catches c > 0x7f
        --tokenizer->pos;
        state = stop_comment;
      }
    } break;

    case stop_comment:
      assert(tokenizer->pos >= start_capture);
      tess_token_deinit(alloc, &res);
      tess_token_init_sn(alloc, &res, comment, tokenizer->input + start_capture,
                         tokenizer->pos - start_capture);
      // res.tag = comment;
      // res.s   = {
      //   std::string{tokenizer->input.data() + start_capture, tokenizer->input.data() + tokenizer->pos}};
      state = stop;
      break;

    case start_string: {
      // std::string::clear() is not guaranteed to retain capacity. We
      // don't wish to shrink buffer between calls to the tokenizer.
      mos_vector_clear(&tokenizer->buf);
      state = in_string;
    } break;

    case in_string: {
      if (tokenizer->pos == end) {
        state = stop_string;
        continue;
      }
      char const c = tokenizer->input[tokenizer->pos++];
      switch (c) {
      case '\\': state = in_string_backslash; break;
      case '"':  state = stop_string; break;
      default:
        if (mos_vector_push_back(alloc, &tokenizer->buf, &c)) {
          if (out_err) tok_error(out_err, out_of_memory, tokenizer->pos);
          return 1;
        }

        break;
      }
    } break;

    case in_string_backslash: {
      if (tokenizer->pos == end) {
        state = stop_string;
        continue;
      }
      char const c = tokenizer->input[tokenizer->pos++];

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
        if (mos_vector_push_back(alloc, &tokenizer->buf, &actual)) {
          if (out_err) tok_error(out_err, out_of_memory, tokenizer->pos);
          return 1;
        }

      } else {
        // unrecognised escape sequence, keep it literal
        char backslash = '\\';
        if (mos_vector_push_back(alloc, &tokenizer->buf, &backslash) ||
            mos_vector_push_back(alloc, &tokenizer->buf, &c)) {
          if (out_err) tok_error(out_err, out_of_memory, tokenizer->pos);
          return 1;
        }
      }
      state = in_string;

    } break;

    case stop_string: {
      tess_token_deinit(alloc, &res);
      tess_token_init_sn(alloc, &res, string, mos_vector_data(&tokenizer->buf),
                         mos_vector_size(&tokenizer->buf));
      state = stop;
    } break;

    case stop: goto finish; break;
    }
  }

finish:

  if (start == state) {
    if (out_err) tok_error(out_err, eof, tokenizer->pos);
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

//

char const *tess_tokenizer_error_tag_to_string(tess_tokenizer_error_tag_t tag) {
#define STRING_ITEM(name, str) [name]                    = str,
  static char const *const tokenizer_error_tag_strings[] = {TOKENIZER_ERROR_TAG_LIST(STRING_ITEM)};
#undef STRING_ITEM
  return tokenizer_error_tag_strings[tag];
}
