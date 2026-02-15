#ifndef TESS_TRANSPILE_V2_H
#define TESS_TRANSPILE_V2_H

#include "alloc.h"
#include "infer.h"

typedef struct transpile transpile;

typedef struct {
    tl_infer_result infer_result;
    int             verbose;
    int             no_line_directive;
    int             is_library;
} transpile_opts;

nodiscard transpile *transpile_create(allocator *, transpile_opts const *) mallocfun;
void                 transpile_destroy(allocator *, transpile **);
int                  transpile_compile(transpile *, str_build *);
void                 transpile_set_verbose(transpile *, int);

// -- stats --

void transpile_get_arena_stats(transpile *, arena_stats *out);

#endif
