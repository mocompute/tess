#include "tess_type.h"
#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <stdio.h>

// -- tess_type allocation and deallocation --

tess_type tess_type_init(tess_type_tag tag) {
    tess_type self;
    alloc_zero(&self);
    self.tag = tag;
    return self;
}

tess_type tess_type_init_type_var(u32 val) {
    tess_type self;
    self          = tess_type_init(type_type_var);
    self.type_var = val;
    return self;
}

tess_type *tess_type_create_type_var(allocator *alloc, u32 val) {
    tess_type *self = alloc_struct(alloc, self);
    *self           = tess_type_init_type_var(val);
    return self;
}

tess_type tess_type_init_tuple() {
    tess_type self = tess_type_init(type_tuple);
    return self;
}

tess_type *tess_type_create_tuple(allocator *alloc, u16 size) {
    tess_type *self     = alloc_struct(alloc, self);
    *self               = tess_type_init_tuple();
    self->elements.size = size;
    if (size) self->elements.v = alloc_calloc(alloc, size, sizeof self->elements.v[0]);
    return self;
}

tess_type tess_type_init_arrow(tess_type *left, tess_type *right) {
    tess_type self = tess_type_init(type_arrow);
    self.left      = left;
    self.right     = right;
    return self;
}

tess_type *tess_type_create_arrow(allocator *alloc, tess_type *left, tess_type *right) {
    tess_type *self = alloc_struct(alloc, self);
    *self           = tess_type_init_arrow(left, right);
    return self;
}

tess_type tess_type_init_user_type(char const *name, tess_type **fields, char const **field_names, u16 n) {

    tess_type self   = tess_type_init(type_user);
    self.name        = name;
    self.fields      = fields;
    self.field_names = field_names;
    self.n_fields    = n;
    return self;
}

tess_type *tess_type_create_user_type(allocator *alloc, char const *name, tess_type **fields,
                                      char const **field_names, u16 n) {

    tess_type *self = alloc_struct(alloc, self);
    *self           = tess_type_init_user_type(name, fields, field_names, n);
    return self;
}

void tess_type_deinit(allocator *alloc, tess_type *self) {
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
        tess_type_deinit(alloc, self->left);
        tess_type_deinit(alloc, self->right);
        break;

    case type_tuple: alloc_free(alloc, self->elements.v); break;
    }

    alloc_invalidate(self);
}

tess_type *tess_type_prim(tess_type_tag tag) {
    static tess_type nil_type    = {.tag = type_nil};
    static tess_type bool_type   = {.tag = type_bool};
    static tess_type int_type    = {.tag = type_int};
    static tess_type float_type  = {.tag = type_float};
    static tess_type string_type = {.tag = type_string};
    static tess_type any_type    = {.tag = type_any};

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

bool tess_type_is_prim(tess_type const *self) {
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

bool tess_type_equal(tess_type const *left, tess_type const *right) {
    return tess_type_compare(left, right) == 0;
}

int tess_type_compare(tess_type const *left, tess_type const *right) {
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
            if ((res = tess_type_compare(left->elements.v[i], right->elements.v[i])) != 0) return res;
        }
        return 0;

    case type_arrow: {
        int res;
        if ((res = tess_type_compare(left->left, right->left)) != 0) return res;
        if ((res = tess_type_compare(left->right, right->right)) != 0) return res;
        return 0;
    }

    case type_user: {
        if (left->n_fields != right->n_fields) return left->n_fields < right->n_fields ? -1 : 1;
        for (u16 i = 0; i < left->n_fields; ++i) {
            int res;
            if ((res = tess_type_compare(left->fields[i], right->fields[i])) != 0) return res;
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

int tess_type_snprint(char *buf, int sz, tess_type const *self) {
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
                len += tess_type_snprint(buf + len, sz - len, self->fields[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += snprintf(null, 0, "%s : ", self->field_names[i]);
                len += tess_type_snprint(null, 0, self->fields[i]);
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
                len += tess_type_snprint(buf + len, sz - len, self->elements.v[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += tess_type_snprint(null, 0, self->elements.v[i]);
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

char *tess_type_to_string(allocator *alloc, tess_type const *type) {
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
