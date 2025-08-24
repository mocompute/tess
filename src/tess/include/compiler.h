#ifndef TESS_COMPILER_H
#define TESS_COMPILER_H

#include "alloc.h"
#include "ast.h"
#include "nodiscard.h"
#include "vector.h"

typedef struct tess_compiler tess_compiler;

// -- allocation and deallocation --

nodiscard tess_compiler *tess_compiler_create(allocator *, ast_pool const *, vec_t *bytes,
                                              allocator *bytes_alloc) mallocfun;
void                     tess_compiler_destroy(tess_compiler **);

// -- operation --

int tess_compiler_compile(tess_compiler *, vec_t const *nodes);

#endif
