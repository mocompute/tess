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
// Memory buffers of tokens created by the tokenizer are managed by
// the tokenizer and are freed by tokenizer_destroy.

nodiscard tokenizer *tokenizer_create(allocator *, char const *, size_t) mallocfun;
void                 tokenizer_destroy(tokenizer **);

void                 tokenizer_error_init(tokenizer_error *);
void                 tokenizer_error_deinit(tokenizer_error *);

// -- parsing --

int tokenizer_next(tokenizer *, token *out, tokenizer_error *);

// -- backtracking --

nodiscard int tokenizer_put_back(tokenizer *, token const *, size_t);

#endif
