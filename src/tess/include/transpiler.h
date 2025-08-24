#ifndef TESS_TRANSPILER_H
#define TESS_TRANSPILER_H

#include "alloc.h"
#include "ast.h"
#include "nodiscard.h"
#include "vector.h"

typedef struct transpiler transpiler;

// -- allocation and deallocation --

nodiscard transpiler *transpiler_create(allocator *, ast_pool const *, vector *bytes,
                                        allocator *bytes_alloc) mallocfun;
void                  transpiler_destroy(transpiler **);

// -- operation --

int transpiler_compile(transpiler *, vector const *nodes);

#endif
