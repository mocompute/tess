#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "token.h"

#include "alloc.h"

#define TOKENIZER_ERROR_TAG_LIST(X)                                                                        \
  X(eof, "eof")                                                                                            \
  X(indent_too_long, "indent_too_long")                                                                    \
  X(invalid_token, "invalid_token")

#define ENUM_ITEM(name, str) name,
typedef enum tess_tokenizer_error_tag_t { TOKENIZER_ERROR_TAG_LIST(ENUM_ITEM) } tess_tokenizer_error_tag_t;
#undef ENUM_ITEM

typedef struct tess_tokenizer_error_t {
  tess_tokenizer_error_tag_t tag;
  size_t                     pos;
} tess_tokenizer_error_t;

typedef struct tess_tokenizer_t tess_tokenizer_t;

// -- allocation and deallocation --

void *tess_tokenizer_alloc(mos_allocator_t *);
void  tess_tokenizer_dealloc(mos_allocator_t *, tess_tokenizer_t *);
void  tess_tokenizer_init(mos_allocator_t *, tess_tokenizer_t *, char const *, size_t);
void  tess_tokenizer_deinit(mos_allocator_t *, tess_tokenizer_t *);

// -- parsing --

int tess_tokenizer_next();

// -- backtracking --

void tess_tokenizer_put_back(mos_allocator_t *, tess_tokenizer_t *, tess_token_t const *, size_t);

// -- utilities --

char const *tess_tokenizer_error_tag_to_string(tess_tokenizer_error_tag_t);

#endif
