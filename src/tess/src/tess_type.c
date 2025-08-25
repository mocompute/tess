#include "tess_type.h"

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type *self, tess_type_tag tag) {
    alloc_zero(self);
    self->tag = tag;
}

void tess_type_init_type_var(tess_type *self, u32 val) {
    tess_type_init(self, type_type_var);
    self->type_var = val;
}

void tess_type_init_tuple(allocator *alloc, tess_type *self) {
    tess_type_init(self, type_tuple);
    vec_init(alloc, &self->tuple, sizeof(tess_type *), 0);
}

void tess_type_init_arrow(tess_type *self, tess_type *left, tess_type *right) {
    tess_type_init(self, type_arrow);
    self->arrow.left  = left;
    self->arrow.right = right;
}

void tess_type_deinit(allocator *alloc, tess_type *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_type_var:
    case type_string:   break;

    case type_arrow:
        tess_type_deinit(alloc, self->arrow.left);
        tess_type_deinit(alloc, self->arrow.right);
        break;

    case type_tuple: vec_deinit(alloc, &self->tuple); break;
    }

    alloc_invalidate(self);
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tess_type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
