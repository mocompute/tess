#ifndef TESS_TOKEN_H
#define TESS_TOKEN_H

#include "alloc.h"

#include <stdint.h>

#define TOKEN_TAG_LIST(X)                                                                                  \
  X(one_newline, "one_newline")                                                                            \
  X(two_newline, "two_newline")                                                                            \
  X(comma, "comma")                                                                                        \
  X(semicolon, "semicolon")                                                                                \
  X(arrow, "arrow")                                                                                        \
  X(open_round, "open_round")                                                                              \
  X(close_round, "close_round")                                                                            \
  X(equal_sign, "equal_sign")                                                                              \
  X(invalid, "invalid")                                                                                    \
  X(newline_indent, "newline_indent")                                                                      \
  X(number, "number")                                                                                      \
  X(symbol, "symbol")                                                                                      \
  X(string, "string")                                                                                      \
  X(comment, "comment")

#define ENUM_ITEM(name, str) name,
typedef enum tess_token_tag_t { TOKEN_TAG_LIST(ENUM_ITEM) } tess_token_tag_t;
#undef ENUM_ITEM

typedef struct tess_token_t {
  union {
    char   *s;
    uint8_t val;
  };
  tess_token_tag_t tag;
} tess_token_t;

// -- allocation and deallocation --

void tess_token_init(tess_token_t *, tess_token_tag_t);
void tess_token_init_v(tess_token_t *, tess_token_tag_t, uint8_t);
int  tess_token_init_s(mos_allocator_t *, tess_token_t *, tess_token_tag_t, char const *);
int  tess_token_init_sn(mos_allocator_t *, tess_token_t *, tess_token_tag_t, char const *, size_t);
void tess_token_deinit(mos_allocator_t *, tess_token_t *);

// -- utilities --

char const *tess_token_tag_to_string(tess_token_tag_t);
char       *tess_token_to_string(mos_allocator_t *, tess_token_t const *);

#endif
