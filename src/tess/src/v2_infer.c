#include "v2_infer.h"

#include "types.h"

struct tl_infer {};

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self = new (alloc, tl_infer);

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **pself) {
    alloc_free(alloc, *pself);
    *pself = null;
}
