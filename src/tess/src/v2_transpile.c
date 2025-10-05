#include "v2_transpile.h"
#include "alloc.h"
#include "str.h"

#define TRANSPILE_ARENA_SIZE 32 * 1024
#define TRANSPILE_BUILD_SIZE 32 * 1024

struct transpile {
    allocator   *parent;
    allocator   *arena;

    tl_type_env *env;
    hashmap     *toplevels; // str => ast_node*

    str_build    build;

    int          verbose;
};

//

int transpile_compile(transpile *self, str_build *out_build) {

    self->build = str_build_init(self->parent, TRANSPILE_BUILD_SIZE);

    str_build_cat(&self->build, S("hello, world\n"));

    if (out_build) {
        *out_build = self->build;
    }
    return 0;
}

//

transpile *transpile_create(allocator *alloc, transpile_opts const *opts) {
    transpile *self = new (alloc, transpile);

    self->parent    = alloc;
    self->arena     = arena_create(alloc, TRANSPILE_ARENA_SIZE);

    self->env       = opts->infer_result.env;
    self->toplevels = opts->infer_result.toplevels;
    self->verbose   = !!opts->verbose;

    return self;
}

void transpile_destroy(allocator *alloc, transpile **p) {
    if (!p || !*p) return;

    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void transpile_set_verbose(transpile *self, int val) {
    self->verbose = val;
}
