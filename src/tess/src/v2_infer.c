#include "v2_infer.h"
#include "v2_type.h"

#include "types.h"

struct tl_infer {
    tl_type_context context;
    tl_type_env    *env;
};

tl_infer *tl_infer_create(allocator *alloc) {
    tl_infer *self = new (alloc, tl_infer);

    self->context  = tl_type_context_empty();
    self->env      = tl_type_env_create(alloc);

    return self;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    tl_type_env_destroy(alloc, &(*p)->env);
    alloc_free(alloc, *p);
    *p = null;
}
