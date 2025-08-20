#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "token.h"

#include "alloc.h"
#include "error.h"
#include "nodiscard.h"

typedef struct tokenizer_error {
  tess_error_tag_t tag;
  size_t           pos;
} tokenizer_error_t;

typedef struct tokenizer tokenizer_t;

// -- allocation and deallocation --
//
// init() with a buffer of input, which must outlive the tokenizer.
//

tokenizer_t  *tokenizer_alloc(mos_allocator *);
void          tokenizer_dealloc(mos_allocator *, tokenizer_t **);
nodiscard int tokenizer_init(mos_allocator *, tokenizer_t *, char const *, size_t);
void          tokenizer_deinit(mos_allocator *, tokenizer_t *);

void          tokenizer_error_init(tokenizer_error_t *);
void          tokenizer_error_deinit(tokenizer_error_t *);

// -- parsing --
//
// next() may allocate memory for string tokens. Caller must token_deinit the [out] token.
//

int tokenizer_next(mos_allocator *, tokenizer_t *, token_t *out, tokenizer_error_t *);

// -- backtracking --

nodiscard int tokenizer_put_back(mos_allocator *, tokenizer_t *, token_t const *, size_t);

#endif
