#include "type.h"
#include "alloc.h"

#include <assert.h>
#include <stdio.h>

// -- tl_type allocation and deallocation --

tl_type *tl_type_create_type_var(allocator *alloc, u32 val) {
    tl_type *self      = alloc_struct(alloc, self);
    self->tag          = type_type_var;
    self->type_var.val = val;

    return self;
}

tl_type *tl_type_create_tuple(allocator *alloc, tl_type_sized elements) {
    tl_type *self        = alloc_struct(alloc, self);
    self->tag            = type_tuple;
    self->tuple.elements = elements;
    return self;
}

tl_type *tl_type_create_labelled_tuple(allocator *alloc, tl_type_sized fields, c_string_csized names) {
    tl_type *self               = alloc_struct(alloc, self);
    self->tag                   = type_labelled_tuple;
    self->labelled_tuple.fields = fields;
    self->labelled_tuple.names  = names;
    return self;
}

tl_type *tl_type_create_arrow(allocator *alloc, tl_type *left, tl_type *right) {
    tl_type *self     = alloc_struct(alloc, self);
    self->tag         = type_arrow;
    self->arrow.left  = left;
    self->arrow.right = right;

    return self;
}

tl_type *tl_type_create_user_type(allocator *alloc, char const *name, tl_type *labelled_tuple) {
    tl_type *self             = alloc_struct(alloc, self);
    self->tag                 = type_user;
    self->user.name           = name;
    self->user.labelled_tuple = labelled_tuple;

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
    // structural equality and total ordering for types

    if (left == right) return 0;

    if (left->tag != right->tag) return left->tag < right->tag ? -1 : 1;

    switch (left->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:    return 0;

    case type_tuple:  {
        struct tlt_tuple const *vleft  = tl_type_tup((tl_type *)left),
                               *vright = tl_type_tup((tl_type *)right);

        if (vleft->elements.size != vright->elements.size)
            return vleft->elements.size < vright->elements.size ? -1 : 1;
        for (u32 i = 0; i < vleft->elements.size; i++) {
            int res;
            if ((res = tl_type_compare(vleft->elements.v[i], vright->elements.v[i])) != 0) return res;
        }
        return 0;
    }

    case type_labelled_tuple: {
        struct tlt_labelled_tuple const *vleft  = tl_type_lt((tl_type *)left),
                                        *vright = tl_type_lt((tl_type *)right);

        if (vleft->fields.size != vright->fields.size)
            return vleft->fields.size < vright->fields.size ? -1 : 1;

        for (u32 i = 0; i < vleft->fields.size; i++) {
            int res = 0;
            if ((res = strcmp(vleft->names.v[i], vright->names.v[i])) != 0) return res;
            if ((res = tl_type_compare(vleft->fields.v[i], vright->fields.v[i])) != 0) return res;
        }
    }
        return 0;

    case type_arrow: {
        struct tlt_arrow const *vleft  = tl_type_arrow((tl_type *)left),
                               *vright = tl_type_arrow((tl_type *)right);
        int res;
        if ((res = tl_type_compare(vleft->left, vright->left)) != 0) return res;
        if ((res = tl_type_compare(vleft->right, vright->right)) != 0) return res;
        return 0;
    }

    case type_user: {
        struct tlt_user const *vleft  = tl_type_user((tl_type *)left),
                              *vright = tl_type_user((tl_type *)right);
        int res                       = 0;
        if ((res == strcmp(vleft->name, vright->name)) != 0) return res;

        return tl_type_compare(vleft->labelled_tuple, vright->labelled_tuple);
    } break;

    case type_type_var: {
        struct tlt_tv const *vleft = tl_type_tv((tl_type *)left), *vright = tl_type_tv((tl_type *)right);

        if (vleft->val == vright->val) return 0;
        return vleft->val < vright->val ? -1 : 1;
    }
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
        struct tlt_user const *v = tl_type_user((tl_type *)self);

        len                      = 0;
        len += snprintf(buf, (size_t)sz, "(%s ", v->name);

        if (buf && sz) len += tl_type_snprint(buf + len, sz - len, v->labelled_tuple);
        else len += tl_type_snprint(null, 0, v->labelled_tuple);

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");
    } break;

    case type_tuple: {
        struct tlt_tuple const *v = tl_type_tup((tl_type *)self);

        len                       = 0;

        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < v->elements.size; ++i) {

            if (buf && sz) {
                len += tl_type_snprint(buf + len, sz - len, v->elements.v[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += tl_type_snprint(null, 0, v->elements.v[i]);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");

    } break;

    case type_labelled_tuple: {
        struct tlt_labelled_tuple const *v = tl_type_lt((tl_type *)self);

        len                                = 0;
        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < v->fields.size; ++i) {

            if (buf && sz) {
                len += snprintf(buf + len, (size_t)(sz - len), "%s : ", v->names.v[i]);
                len += tl_type_snprint(buf + len, sz - len, v->fields.v[i]);
                len += snprintf(buf + len, (size_t)(sz - len), ", ");
            } else {
                len += snprintf(null, 0, "%s : ", v->names.v[i]);
                len += tl_type_snprint(null, 0, v->fields.v[i]);
                len += snprintf(null, 0, ", ");
            }
        }

        if (buf && sz) len += snprintf(buf + len, (size_t)(sz - len), ")");
        else len += snprintf(null, 0, ")");

    } break;

    case type_arrow: {
        struct tlt_arrow const *v = tl_type_arrow((tl_type *)self);

        len                       = 0;
        if (buf && sz) {
            len += tl_type_snprint(buf, sz, v->left);
            len += snprintf(buf + len, (size_t)(sz - len), " -> ");
            len += tl_type_snprint(buf + len, sz - len, v->right);
        } else {
            len += tl_type_snprint(null, 0, v->left);
            len += snprintf(null, 0, " -> ");
            len += tl_type_snprint(null, 0, v->right);
        }

    } break;

    case type_type_var: {
        struct tlt_tv const *v = tl_type_tv((tl_type *)self);
        len                    = snprintf(buf, (size_t)sz, "tv%u", v->val);
    } break;
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

    case type_tuple:  {

        // labelled tuples satisfy plain tuples if their types match.
        if (type_tuple != candidate->tag && type_labelled_tuple != candidate->tag) return false;

        struct tlt_array const *vreq  = tl_type_arr((tl_type *) requires),
                               *vcand = tl_type_arr((tl_type *)candidate);

        if (vreq->elements.size != vcand->elements.size) return false;

        for (u32 i = 0; i < vreq->elements.size; ++i)
            if (!tl_type_satisfies(vreq->elements.v[i], vcand->elements.v[i])) return false;

        return true;
    }

    case type_labelled_tuple: {
        // plain tuples do not satisfy labelled tuples
        if (type_labelled_tuple != candidate->tag) return false;

        struct tlt_array const *vreqarr  = tl_type_arr((tl_type *) requires),
                               *vcandarr = tl_type_arr((tl_type *)candidate);

        if (vreqarr->elements.size != vcandarr->elements.size) return false;

        struct tlt_labelled_tuple const *vreq  = tl_type_lt((tl_type *) requires),
                                        *vcand = tl_type_lt((tl_type *)candidate);

        // names and types must match
        for (u32 i = 0; i < vreqarr->elements.size; ++i) {
            if (0 != strcmp(vreq->names.v[i], vcand->names.v[i])) return false;
            if (!tl_type_satisfies(vreqarr->elements.v[i], vcandarr->elements.v[i])) return false;
        }

        return true;
    }

    case type_arrow: {
        struct tlt_arrow const *vreq  = tl_type_arrow((tl_type *) requires),
                               *vcand = tl_type_arrow((tl_type *)candidate);
        return candidate->tag == type_arrow && tl_type_satisfies(vreq->left, vcand->left) &&
               tl_type_satisfies(vreq->right, vcand->right);
    }

    case type_user: {
        // same-named user types satisfy, though in that case they are expected to be the same identity
        assert(requires == candidate);
        return 0 == strcmp(requires->user.name, candidate->user.name);
    }

    case type_type_var:
        // are never satisfied
        return false;

    case type_any:
        // are always satisfied
        return true;
    }
}

tl_type *tl_type_find_field_type(tl_type const *user_type, char const *field_name) {
    struct tlt_user           *v          = tl_type_user((tl_type *)user_type);
    struct tlt_labelled_tuple *lt         = tl_type_lt(v->labelled_tuple);
    tl_type                   *field_type = null;
    for (u32 i = 0; i < lt->names.size; ++i) {
        if (0 == strcmp(lt->names.v[i], field_name)) {
            field_type = lt->fields.v[i];
            break;
        }
    }
    return field_type;
}

bool tl_type_contains(tl_type const *haystack, tl_type const *needle) {

    if (haystack == needle || tl_type_equal(haystack, needle)) return true;

    switch (haystack->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_type_var:
    case type_any:            return false;

    case type_tuple:
    case type_labelled_tuple: {
        struct tlt_array *v = tl_type_arr((tl_type *)haystack);
        for (u32 i = 0; i < v->elements.size; ++i) {
            if (tl_type_contains(v->elements.v[i], needle)) return true;
        }
        return false;
    }

    case type_arrow: {
        struct tlt_arrow *v = tl_type_arrow((tl_type *)haystack);
        return tl_type_contains(v->left, needle) || tl_type_contains(v->right, needle);
    }

    case type_user: {
        struct tlt_user *v = tl_type_user((tl_type *)haystack);
        return tl_type_contains(v->labelled_tuple, needle);
    }
    }
}

//

struct tlt_array *tl_type_arr(tl_type *t) {
    assert(t->tag == type_tuple || t->tag == type_labelled_tuple);
    return &t->array;
}

struct tlt_tuple *tl_type_tup(tl_type *t) {
    assert(t->tag == type_tuple);
    return &t->tuple;
}

struct tlt_labelled_tuple *tl_type_lt(tl_type *t) {
    assert(t->tag == type_labelled_tuple);
    return &t->labelled_tuple;
}

struct tlt_arrow *tl_type_arrow(tl_type *t) {
    assert(t->tag == type_arrow);
    return &t->arrow;
}

struct tlt_user *tl_type_user(tl_type *t) {
    assert(t->tag == type_user);
    return &t->user;
}

struct tlt_tv *tl_type_tv(tl_type *t) {
    assert(t->tag == type_type_var);
    return &t->type_var;
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *type_tag_to_string(tl_type_tag tag) {
    static char const *const strings[] = {TL_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
