#ifndef TESS_TOKEN_H
#define TESS_TOKEN_H

#include "alloc.h"

#include <stdint.h>

#define TESS_TOKEN_TAGS(X)                                                                                 \
  X(tess_tok_one_newline, "one_newline")                                                                   \
  X(tess_tok_two_newline, "two_newline")                                                                   \
  X(tess_tok_comma, "comma")                                                                               \
  X(tess_tok_semicolon, "semicolon")                                                                       \
  X(tess_tok_arrow, "arrow")                                                                               \
  X(tess_tok_open_round, "open_round")                                                                     \
  X(tess_tok_close_round, "close_round")                                                                   \
  X(tess_tok_equal_sign, "equal_sign")                                                                     \
  X(tess_tok_invalid, "invalid")                                                                           \
  X(tess_tok_newline_indent, "newline_indent")                                                             \
  X(tess_tok_number, "number")                                                                             \
  X(tess_tok_symbol, "symbol")                                                                             \
  X(tess_tok_string, "string")                                                                             \
  X(tess_tok_comment, "comment")

#define TESS_ENUM(name, str) name,
typedef enum token_tag { TESS_TOKEN_TAGS(TESS_ENUM) } token_tag_t;
#undef TESS_ENUM

typedef struct token {
  union {
    char   *s;
    uint8_t val;
  };
  token_tag_t tag;
} token_t;

// -- allocation and deallocation --

void token_init(token_t *, token_tag_t);
void token_init_v(token_t *, token_tag_t, uint8_t);
int  token_init_s(mos_allocator_t *, token_t *, token_tag_t, char const *);
int  token_init_sn(mos_allocator_t *, token_t *, token_tag_t, char const *, size_t);
void token_deinit(mos_allocator_t *, token_t *);

// -- utilities --

char const *token_tag_to_string(token_tag_t);
char       *token_to_string(mos_allocator_t *, token_t const *);

#endif
