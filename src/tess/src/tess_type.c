#include "tess_type.h"
#include "alloc.h"
#include "vector.h"

#include <assert.h>
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

struct tess_type *tess_type_create_type_var(allocator *alloc, u32 val) {
    struct tess_type *self = alloc_struct(alloc, self);
    *self                  = tess_type_init_type_var(val);
    return self;
}

struct tess_type tess_type_init_tuple() {
    struct tess_type self = tess_type_init(type_tuple);
    return self;
}

struct tess_type *tess_type_create_tuple(allocator *alloc, u16 size) {
    struct tess_type *self = alloc_struct(alloc, self);
    *self                  = tess_type_init_tuple();
    self->n_elements       = size;
    if (size) self->elements = alloc_calloc(alloc, size, sizeof *self->elements);
    return self;
}

struct tess_type tess_type_init_arrow(struct tess_type *left, struct tess_type *right) {
    struct tess_type self = tess_type_init(type_arrow);
    self.left             = left;
    self.right            = right;
    return self;
}

struct tess_type *tess_type_create_arrow(allocator *alloc, struct tess_type *left,
                                         struct tess_type *right) {
    struct tess_type *self = alloc_struct(alloc, self);
    *self                  = tess_type_init_arrow(left, right);
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
        // Note: cast away const
        tess_type_deinit(alloc, (struct tess_type *)self->left);
        tess_type_deinit(alloc, (struct tess_type *)self->right);
        break;

    case type_tuple: alloc_free(alloc, self->elements); break;
    }

    alloc_invalidate(self);
}

struct tess_type const *tess_type_prim(tess_type_tag tag) {
    static struct tess_type nil_type    = {{0}, type_nil};
    static struct tess_type bool_type   = {{0}, type_bool};
    static struct tess_type int_type    = {{0}, type_int};
    static struct tess_type float_type  = {{0}, type_float};
    static struct tess_type string_type = {{0}, type_string};

    switch (tag) {
    case type_nil:    return &nil_type;
    case type_bool:   return &bool_type;
    case type_int:    return &int_type;
    case type_float:  return &float_type;
    case type_string: return &string_type;
    case type_tuple:
    case type_arrow:
    case type_type_var:
        assert(false);
        exit(1);
        break;
    }
    assert(false);
}

bool tess_type_is_prim(struct tess_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:   return true;
    case type_tuple:
    case type_arrow:
    case type_type_var: return false;
    }
    assert(false);
}

bool tess_type_equal(struct tess_type const *left, struct tess_type const *right) {
    if (left->tag != right->tag) return false;

    switch (left->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return true;

    case type_tuple:  {

        if (left->n_elements != right->n_elements) return false;

        for (size_t i = 0; i < left->n_elements; ++i)
            if (!tess_type_equal(left->elements[i], right->elements[i])) return false;

        return true;

    } break;

    case type_arrow:
        return tess_type_equal(left->left, right->left) && tess_type_equal(left->right, right->right);

    case type_type_var: return left->type_var == right->type_var;
    }
    assert(false);
    exit(1);
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
        len = 0;

        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < self->n_elements; ++i) {

            if (buf && sz) {
                len += tess_type_snprint(buf + len, sz - len, self->elements[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += tess_type_snprint(null, 0, self->elements[i]);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");

    } break;

    case type_arrow: {
        len = 0;
        if (buf && sz) {
            len += tess_type_snprint(buf, sz, self->left);
            len += snprintf(buf + len, (size_t)(sz - len), " -> ");
            len += tess_type_snprint(buf + len, sz - len, self->right);
        } else {
            len += tess_type_snprint(null, 0, self->left);
            len += snprintf(null, 0, " -> ");
            len += tess_type_snprint(null, 0, self->right);
        }

    } break;

    case type_type_var: len = snprintf(buf, (size_t)sz, "tv%u", self->type_var); break;
    }

    return len;
}

char *tess_type_to_string(allocator *alloc, struct tess_type const *type) {
    int   len = tess_type_snprint(null, 0, type) + 1;
    char *out = alloc_malloc(alloc, (size_t)len);
    tess_type_snprint(out, len, type);
    return out;
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tess_type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
