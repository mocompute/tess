#ifndef TESS_TRANSPILER_H
#define TESS_TRANSPILER_H

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "type_registry.h"

typedef struct transpiler transpiler;

// -- allocation and deallocation --

nodiscard transpiler *transpiler_create(allocator *, char_array *bytes, type_registry *) mallocfun;
void                  transpiler_destroy(transpiler **);

// -- operation --

int  transpiler_compile(transpiler *, struct ast_node **nodes, u32);
void transpiler_set_verbose(transpiler *, bool);

#endif
