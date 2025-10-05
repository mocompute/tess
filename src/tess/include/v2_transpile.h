#ifndef TESS_TRANSPILE_V2_H
#define TESS_TRANSPILE_V2_H

#include "alloc.h"
#include "v2_infer.h"
#include "v2_type.h"

typedef struct transpile transpile;

typedef struct {
    tl_type_env *env;
    hashmap     *toplevels; // str => ast_node*
} transpile_opts;

nodiscard transpile *transpile_create(allocator *, transpile_opts const *) mallocfun;
void                 transpile_destroy(allocator *, transpile **);

#endif
