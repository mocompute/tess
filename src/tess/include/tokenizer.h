#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "token.h"

#include "alloc.h"
#include "error.h"
#include "nodiscard.h"

typedef struct tokenizer_error {
  tess_error_tag tag;
  size_t         pos;
} tokenizer_error;

typedef struct tokenizer tokenizer;

// -- allocation and deallocation --
//
// init() with a buffer of input, which must outlive the tokenizer.
//

tokenizer    *tokenizer_alloc(allocator *);
void          tokenizer_dealloc(allocator *, tokenizer **);
nodiscard int tokenizer_init(allocator *, tokenizer *, char const *, size_t);
void          tokenizer_deinit(allocator *, tokenizer *);

void          tokenizer_error_init(tokenizer_error *);
void          tokenizer_error_deinit(tokenizer_error *);

// -- parsing --
//
// next() may allocate memory for string tokens. Caller must token_deinit the [out] token.
//

int tokenizer_next(allocator *, tokenizer *, token *out, tokenizer_error *);

// -- backtracking --

nodiscard int tokenizer_put_back(allocator *, tokenizer *, token const *, size_t);

#endif
