#include "type.h"
#include "alloc.h"

#include <assert.h>
#include <stdio.h>

// -- tl_type allocation and deallocation --

tl_type *tl_type_create_type_var(allocator *alloc, u32 val) {
    tl_type *self  = alloc_struct(alloc, self);
    self->tag      = type_type_var;
    self->type_var = val;

    return self;
}

tl_type *tl_type_create_tuple(allocator *alloc, tl_type_sized elements) {
    tl_type *self  = alloc_struct(alloc, self);
    self->tag      = type_tuple;
    self->elements = elements;
    return self;
}

tl_type *tl_type_create_arrow(allocator *alloc, tl_type *left, tl_type *right) {
    tl_type *self = alloc_struct(alloc, self);
    self->tag     = type_arrow;
    self->left    = left;
    self->right   = right;

    return self;
}

tl_type *tl_type_create_user_type(allocator *alloc, char const *name, tl_type *labelled_tuple) {
    tl_type *self        = alloc_struct(alloc, self);
    self->tag            = type_user;
    self->name           = name;
    self->labelled_tuple = labelled_tuple;

    return self;
}

bool tl_type_is_prim(tl_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:            return true;

    case type_user:
    case type_tuple:
    case type_labelled_tuple:
    case type_arrow:
    case type_type_var:       return false;
    }
    assert(false);
}

bool tl_type_equal(tl_type const *left, tl_type const *right) {
    return tl_type_compare(left, right) == 0;
}

int tl_type_compare(tl_type const *left, tl_type const *right) {
    // structural equality and total ordering for tess_types

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

    case type_labelled_tuple:
        if (left->fields.size != right->fields.size) return left->fields.size < right->fields.size ? -1 : 1;

        for (u32 i = 0; i < left->fields.size; i++) {
            int res = 0;
            if ((res = strcmp(left->names.v[i], right->names.v[i])) != 0) return res;
            if ((res = tl_type_compare(left->fields.v[i], right->fields.v[i])) != 0) return res;
        }
        return 0;

    case type_arrow: {
        int res;
        if ((res = tl_type_compare(left->left, right->left)) != 0) return res;
        if ((res = tl_type_compare(left->right, right->right)) != 0) return res;
        return 0;
    }

    case type_user: {
        int res = 0;
        if ((res == strcmp(left->name, right->name)) != 0) return res;

        return tl_type_compare(left->labelled_tuple, right->labelled_tuple);
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
        len += snprintf(buf, (size_t)sz, "(%s ", self->name);

        tl_type tmp = *self;
        tmp.tag     = type_labelled_tuple;
        if (buf && sz) len += tl_type_snprint(buf + len, sz - len, &tmp);

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

    case type_labelled_tuple: {

        len = 0;
        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < self->labelled_tuple->fields.size; ++i) {

            if (buf && sz) {
                len += snprintf(buf + len, (size_t)(sz - len), "%s : ", self->names.v[i]);
                len += tl_type_snprint(buf + len, sz - len, self->fields.v[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += snprintf(null, 0, "%s : ", self->names.v[i]);
                len += tl_type_snprint(null, 0, self->fields.v[i]);
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

bool tl_type_satisfies(tl_type const *requires, tl_type const *candidate) {
    // type variables are not satisfied by any type

    if (requires == candidate) return true; // self-satisfied

    switch (requires->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return (requires->tag == candidate->tag);

    case type_tuple:
        // labelled tuples satisfy plain tuples if their types match.
        if (type_tuple != candidate->tag && type_labelled_tuple != candidate->tag) return false;
        if (requires->elements.size != candidate->elements.size) return false;

        for (u32 i = 0; i < requires->elements.size; ++i)
            if (!tl_type_satisfies(requires->elements.v[i], candidate->elements.v[i])) return false;

        return true;

    case type_labelled_tuple:
        // plain tuples do not satisfy labelled tuples
        if (type_labelled_tuple != candidate->tag) return false;
        if (requires->elements.size != candidate->elements.size) return false;

        // names and types must match
        for (u32 i = 0; i < requires->elements.size; ++i) {
            if (0 != strcmp(requires->names.v[i], candidate->names.v[i])) return false;
            if (!tl_type_satisfies(requires->elements.v[i], candidate->elements.v[i])) return false;
        }

        return true;

        break;

    case type_arrow:
        return candidate->tag == type_arrow && tl_type_satisfies(requires->left, candidate->left) &&
               tl_type_satisfies(requires->right, candidate->right);

    case type_user:
        // same-named user types satisfy, though in that case they are expected to be the same identity
        assert(requires == candidate);
        return 0 == strcmp(requires->name, candidate->name);

    case type_type_var:
        // are never satisfied
        return false;

    case type_any:
        // are always satisfied
        return true;
    }
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tl_type_tag tag) {
    static char const *const strings[] = {TL_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
