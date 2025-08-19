#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "alloc.h"

#include <stdint.h>

typedef enum tess_token_tag_t : uint8_t {
  one_newline,
  two_newline,
  comma,
  semicolon,
  arrow,
  open_round,
  close_round,
  equal_sign,
  invalid,
  newline_indent,
  number,
  symbol,
  string,
  comment,
} tess_token_tag_t;

struct tess_token_t {
  tess_token_tag_t tag;
  union {
    char   *s;
    uint8_t val;
  };
};

void tess_token_deinit(mos_allocator_t *, struct tess_token_t *);

#endif
