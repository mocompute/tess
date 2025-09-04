#ifndef TESS_TOKENIZER_H
#define TESS_TOKENIZER_H

#include "token.h"

#include "alloc.h"
#include "error.h"

typedef struct tokenizer_error {
    tess_error_tag tag;
    size_t         pos;
} tokenizer_error;

typedef struct tokenizer tokenizer;

// -- allocation and deallocation --
//
// Memory buffers of tokens created by the tokenizer are managed by
// the tokenizer and are freed by tokenizer_destroy.

nodiscard tokenizer *tokenizer_create(allocator *, char_cslice) mallocfun;
void                 tokenizer_destroy(tokenizer **);

// -- parsing --

nodiscard int tokenizer_next(tokenizer *, token *out, tokenizer_error *);

// -- backtracking --

void tokenizer_put_back(tokenizer *, token const *, size_t);

#endif
