#include "tess_type.h"
#include "vector.h"
#include <stdio.h>

// -- tess_type allocation and deallocation --

struct tess_type tess_type_init(tess_type_tag tag) {
    struct tess_type self;
    alloc_zero(&self);
    self.tag = tag;
    return self;
}

struct tess_type tess_type_init_type_var(u32 val) {
    struct tess_type self;
    self          = tess_type_init(type_type_var);
    self.type_var = val;
    return self;
}

struct tess_type tess_type_init_tuple(allocator *alloc) {
    struct tess_type self = tess_type_init(type_tuple);
    vec_init(alloc, &self.tuple, 0, sizeof(struct tess_type *));
    return self;
}

struct tess_type tess_type_init_arrow(struct tess_type *left, struct tess_type *right) {
    struct tess_type self = tess_type_init(type_arrow);
    self.arrow.left       = left;
    self.arrow.right      = right;
    return self;
}

void tess_type_deinit(allocator *alloc, struct tess_type *self) {
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

int tess_type_snprint(char *buf, int sz, struct tess_type const *self) {
    int len = -1;

    if (null == self) return snprintf(buf, (size_t)sz, "[null]");

    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: len = snprintf(buf, (size_t)sz, "%s", type_tag_to_string(self->tag)); break;

    case type_tuple:  {
        len                                = 0;
        struct tess_type const *const *it  = vec_cbegin(&self->tuple);
        struct tess_type const *const *end = vec_cend(&self->tuple);
        len += snprintf(buf, (size_t)sz, "(");
        while (it != end) {
            if (buf && sz) {
                len += tess_type_snprint(buf + len, sz - len, *it++);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += tess_type_snprint(null, 0, *it++);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf, (size_t)sz, ")");
        else len += snprintf(null, 0, ")");

    } break;

    case type_arrow: {
        len = 0;
        if (buf && sz) {
            len += tess_type_snprint(buf, sz, self->arrow.left);
            len += snprintf(buf + len, (size_t)(sz - len), " -> ");
            len += tess_type_snprint(buf + len, sz - len, self->arrow.left);
        } else {
            len += tess_type_snprint(null, 0, self->arrow.left);
            len += snprintf(null, 0, " -> ");
            len += tess_type_snprint(null, 0, self->arrow.left);
        }

    } break;
    case type_type_var: len = snprintf(buf, (size_t)sz, "tv%u", self->type_var); break;
    }

    return len;
}

char *tess_type_to_string(allocator *alloc, struct tess_type const *self) {
    int len = tess_type_snprint(null, 0, self);
    if (len <= 0) return null;
    char *out = alloc_malloc(alloc, (size_t)len + 1);
    if (tess_type_snprint(out, len + 1, self) < 0) {
        alloc_free(alloc, out);
        return null;
    }

    return out;
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tess_type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
