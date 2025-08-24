#ifndef TESS_COMPILER_H
#define TESS_COMPILER_H

#include "alloc.h"
#include "ast.h"
#include "nodiscard.h"
#include "vector.h"

typedef struct transpiler transpiler;

// -- allocation and deallocation --

nodiscard transpiler *transpiler_create(allocator *, ast_pool const *, vec_t *bytes,
                                        allocator *bytes_alloc) mallocfun;
void                  transpiler_destroy(transpiler **);

// -- operation --

int transpiler_compile(transpiler *, vec_t const *nodes);

#endif
