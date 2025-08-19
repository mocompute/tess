#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "token.h"

#include "alloc.h"
#include "error.h"

typedef struct tess_tokenizer_error {
  tess_error_tag_t tag;
  size_t           pos;
} tess_tokenizer_error_t;

typedef struct tess_tokenizer tess_tokenizer_t;

// -- allocation and deallocation --
//
// init() with a buffer of input, which must outlive the tokenizer.
//

tess_tokenizer_t *tess_tokenizer_alloc(mos_allocator_t *);
void              tess_tokenizer_dealloc(mos_allocator_t *, tess_tokenizer_t *);
void              tess_tokenizer_init(mos_allocator_t *, tess_tokenizer_t *, char const *, size_t);
void              tess_tokenizer_deinit(mos_allocator_t *, tess_tokenizer_t *);

// -- parsing --
//
// next() may allocate memory for string tokens. Caller must token_deinit the [out] token.
//

int tess_tokenizer_next(mos_allocator_t *, tess_tokenizer_t *, tess_token_t *out, tess_tokenizer_error_t *);

// -- backtracking --

void tess_tokenizer_put_back(mos_allocator_t *, tess_tokenizer_t *, tess_token_t const *, size_t);

#endif
