#include "tess_type.h"

struct tess_type_pool {
    allocator    *alloc;
    struct vector data; // tess_type
};

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

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type *self, type_tag tag) {
    alloc_zero(self);
    self->tag = tag;
}

void tess_type_init_type_var(tess_type *self, u32 val) {
    alloc_zero(self);
    self->tag = type_type_var;
    self->val = val;
}

int tess_type_init_tuple(allocator *alloc, tess_type *self) {
    alloc_zero(self);
    self->tag = type_tuple;
    vec_init(alloc, &self->tuple, sizeof(tess_type_h), 0);
    return 0;
}

void tess_type_init_arrow(tess_type *ty) {
    alloc_zero(ty);
    ty->tag = type_arrow;
}

void tess_type_deinit(allocator *alloc, tess_type *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_arrow:
    case type_type_var:
    case type_string:   break;
    case type_tuple:    vec_deinit(alloc, &self->tuple); break;
    }
    alloc_invalidate(self);
}

char const *type_tag_to_string(type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
