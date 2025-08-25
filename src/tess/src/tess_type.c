#include "tess_type.h"

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type *self, tess_type_tag tag) {
    alloc_zero(self);
    self->tag = tag;
}

void tess_type_init_type_var(tess_type *self, u32 val) {
    alloc_zero(self);
    self->tag = type_type_var;
    self->val = val;
}

int tess_type_init_tuple(allocator *alloc, struct tess_type *self) {
    alloc_zero(self);
    self->tag = type_tuple;
    vec_init(alloc, &self->tuple, sizeof(tess_type *), 0);
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

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tess_type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
