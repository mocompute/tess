#include "alloc.h"
#include "type.h"
#include "vector.h"

#include <assert.h>
#include <stdio.h>

// -- tl_type allocation and deallocation --

tl_type tl_type_init(tl_type_tag tag) {
    tl_type self;
    alloc_zero(&self);
    self.tag = tag;
    return self;
}

tl_type tl_type_init_type_var(u32 val) {
    tl_type self;
    self          = tl_type_init(type_type_var);
    self.type_var = val;
    return self;
}

tl_type *tl_type_create_type_var(allocator *alloc, u32 val) {
    tl_type *self = alloc_struct(alloc, self);
    *self         = tl_type_init_type_var(val);
    return self;
}

tl_type tl_type_init_tuple() {
    tl_type self = tl_type_init(type_tuple);
    return self;
}

tl_type *tl_type_create_tuple(allocator *alloc, u16 size) {
    tl_type *self       = alloc_struct(alloc, self);
    *self               = tl_type_init_tuple();
    self->elements.size = size;
    if (size) self->elements.v = alloc_calloc(alloc, size, sizeof self->elements.v[0]);
    return self;
}

tl_type tl_type_init_arrow(tl_type *left, tl_type *right) {
    tl_type self = tl_type_init(type_arrow);
    self.left    = left;
    self.right   = right;
    return self;
}

tl_type *tl_type_create_arrow(allocator *alloc, tl_type *left, tl_type *right) {
    tl_type *self = alloc_struct(alloc, self);
    *self         = tl_type_init_arrow(left, right);
    return self;
}

tl_type tl_type_init_user_type(char const *name, tl_type **fields, char const **field_names, u16 n) {

    tl_type self     = tl_type_init(type_user);
    self.name        = name;
    self.fields      = fields;
    self.field_names = field_names;
    self.n_fields    = n;
    return self;
}

tl_type *tl_type_create_user_type(allocator *alloc, char const *name, tl_type **fields,
                                  char const **field_names, u16 n) {

    tl_type *self = alloc_struct(alloc, self);
    *self         = tl_type_init_user_type(name, fields, field_names, n);
    return self;
}

void tl_type_deinit(allocator *alloc, tl_type *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_type_var:
    case type_any:
    case type_string:   break;

    case type_user:
        alloc_free(alloc, self->fields);
        alloc_free(alloc, self->field_names);
        break;

    case type_arrow:
        // Note: cast away const
        tl_type_deinit(alloc, self->left);
        tl_type_deinit(alloc, self->right);
        break;

    case type_tuple: alloc_free(alloc, self->elements.v); break;
    }

    alloc_invalidate(self);
}

tl_type *tl_type_prim(tl_type_tag tag) {
    static tl_type nil_type    = {.tag = type_nil};
    static tl_type bool_type   = {.tag = type_bool};
    static tl_type int_type    = {.tag = type_int};
    static tl_type float_type  = {.tag = type_float};
    static tl_type string_type = {.tag = type_string};
    static tl_type any_type    = {.tag = type_any};

    switch (tag) {
    case type_nil:    return &nil_type;
    case type_bool:   return &bool_type;
    case type_int:    return &int_type;
    case type_float:  return &float_type;
    case type_string: return &string_type;
    case type_any:    return &any_type;
    case type_tuple:
    case type_arrow:
    case type_user:
    case type_type_var:
        assert(false);
        exit(1);
        break;
    }
    assert(false);
}

bool tl_type_is_prim(tl_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:      return true;
    case type_user:
    case type_tuple:
    case type_arrow:
    case type_type_var: return false;
    }
    assert(false);
}

bool tl_type_equal(tl_type const *left, tl_type const *right) {
    return tl_type_compare(left, right) == 0;
}

int tl_type_compare(tl_type const *left, tl_type const *right) {
    // structural equality for tl_types - TODO may not be necessary
    // because we use reference equality for types.
    if (left->tag != right->tag) return left->tag < right->tag ? -1 : 1;

    switch (left->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:    return 0;

    case type_tuple:
        if (left->elements.size != right->elements.size)
            return left->elements.size < right->elements.size ? -1 : 1;
        for (u32 i = 0; i < left->elements.size; i++) {
            int res;
            if ((res = tl_type_compare(left->elements.v[i], right->elements.v[i])) != 0) return res;
        }
        return 0;

    case type_arrow: {
        int res;
        if ((res = tl_type_compare(left->left, right->left)) != 0) return res;
        if ((res = tl_type_compare(left->right, right->right)) != 0) return res;
        return 0;
    }

    case type_user: {
        if (left->n_fields != right->n_fields) return left->n_fields < right->n_fields ? -1 : 1;
        for (u16 i = 0; i < left->n_fields; ++i) {
            int res;
            if ((res = tl_type_compare(left->fields[i], right->fields[i])) != 0) return res;
        }

        for (u16 i = 0; i < left->n_fields; ++i) {
            int res;
            if ((res = strcmp(left->field_names[i], right->field_names[i])) != 0) return res;
        }
        return 0;

    } break;

    case type_type_var:
        if (left->type_var == right->type_var) return 0;
        return left->type_var < right->type_var ? -1 : 1;
    }
}

int tl_type_snprint(char *buf, int sz, tl_type const *self) {
    int len = -1;

    if (null == self) return snprintf(buf, (size_t)sz, "[null]");

    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:    len = snprintf(buf, (size_t)sz, "%s", type_tag_to_string(self->tag)); break;

    case type_user:   {
        len = 0;
        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < self->n_fields; ++i) {

            if (buf && sz) {
                len += snprintf(buf + len, (size_t)(sz - len), "%s : ", self->field_names[i]);
                len += tl_type_snprint(buf + len, sz - len, self->fields[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += snprintf(null, 0, "%s : ", self->field_names[i]);
                len += tl_type_snprint(null, 0, self->fields[i]);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");
    } break;

    case type_tuple: {
        len = 0;

        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < self->elements.size; ++i) {

            if (buf && sz) {
                len += tl_type_snprint(buf + len, sz - len, self->elements.v[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += tl_type_snprint(null, 0, self->elements.v[i]);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");

    } break;

    case type_arrow: {
        len = 0;
        if (buf && sz) {
            len += tl_type_snprint(buf, sz, self->left);
            len += snprintf(buf + len, (size_t)(sz - len), " -> ");
            len += tl_type_snprint(buf + len, sz - len, self->right);
        } else {
            len += tl_type_snprint(null, 0, self->left);
            len += snprintf(null, 0, " -> ");
            len += tl_type_snprint(null, 0, self->right);
        }

    } break;

    case type_type_var: len = snprintf(buf, (size_t)sz, "tv%u", self->type_var); break;
    }

    return len;
}

char *tl_type_to_string(allocator *alloc, tl_type const *type) {
    int   len = tl_type_snprint(null, 0, type) + 1;
    char *out = alloc_malloc(alloc, (size_t)len);
    tl_type_snprint(out, len, type);
    return out;
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tl_type_tag tag) {
    static char const *const strings[] = {TL_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
