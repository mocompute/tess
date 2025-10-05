#include "v2_transpile.h"

struct transpile {};

transpile *transpile_create(allocator *alloc, transpile_opts const *opts) {
    transpile *self = new (alloc, transpile);

    (void)opts;

    return self;
}

void transpile_destroy(allocator *alloc, transpile **p) {
    if (!p || !*p) return;
    alloc_free(alloc, *p);
    *p = null;
}
