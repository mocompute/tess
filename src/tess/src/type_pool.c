#include "type_pool.h"
#include "tess_type.h"

// -- pool allocation and deallocation --

tess_type_pool *tess_type_pool_create(allocator *alloc) {
    tess_type_pool *self = alloc_calloc(alloc, 1, sizeof(*self));

    self->alloc          = alloc;

    vec_init(alloc, &self->data, sizeof(tess_type), 32);

    return self;
}

void tess_type_pool_destroy(tess_type_pool **self) {

    // deinit all the types
    tess_type       *it  = vec_begin(&(*self)->data);
    tess_type const *end = vec_end(&(*self)->data);
    while (it != end) tess_type_deinit((*self)->alloc, it++);

    vec_deinit((*self)->alloc, &(*self)->data);
    alloc_free((*self)->alloc, *self);
    *self = null;
}
