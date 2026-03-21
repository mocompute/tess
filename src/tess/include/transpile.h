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
    int             bounds_check;
    str             lib_name; // library name for namespaced init (e.g., "foo" -> tl_init_foo)
    str             version;  // compiler version string for generated file header
} transpile_opts;

nodiscard transpile *transpile_create(allocator *, transpile_opts const *) mallocfun;
void                 transpile_destroy(allocator *, transpile **);
int                  transpile_compile(transpile *, str_build *);
void                 transpile_set_verbose(transpile *, int);

// -- c_export header generation --

// Generate a C header with prototypes for [[c_export]] functions.
// Returns 1 if header was generated (has exports), 0 if no exports found.
int transpile_generate_header(transpile *, str_build *out_header, str guard_name);

// -- stats --

void transpile_get_arena_stats(transpile *, arena_stats *out);

#endif
