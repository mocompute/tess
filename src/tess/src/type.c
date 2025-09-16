#include "type.h"
#include "alloc.h"
#include "hash.h"
#include "hashmap.h"

#include <assert.h>
#include <stdio.h>

// -- tl_type allocation and deallocation --

tl_type *tl_type_create(allocator *alloc, tl_type_tag tag) {
    tl_type *self = alloc_struct(alloc, self);
    self->tag     = tag;
    return self;
}

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

tl_type *tl_type_create_arrow(allocator *alloc, tl_type *left, tl_type *right, int is_lambda) {
    tl_type *self     = alloc_struct(alloc, self);
    self->tag         = type_arrow;
    self->arrow.left  = left;
    self->arrow.right = right;
    self->arrow.flags = 0;
    if (is_lambda) SET_BIT(self->arrow.flags, TL_TYPE_ARROW_LAMBDA);

    return self;
}

tl_type *tl_type_create_user_type(allocator *alloc, char const *name, tl_type *labelled_tuple) {
    tl_type *self             = alloc_struct(alloc, self);
    self->tag                 = type_user;
    self->user.name           = name;
    self->user.labelled_tuple = labelled_tuple;

    return self;
}

tl_type *tl_type_clone_impl(allocator *alloc, tl_type const *orig, tl_make_typevar_fun make_typevar,
                            void *ctx, hashmap **tvmap) {

    tl_type *clone = tl_type_create(alloc, orig->tag);

    switch (clone->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: break;

    case type_tuple:
        clone->array.elements.size = orig->array.elements.size;
        clone->array.elements.v =
          alloc_malloc(alloc, clone->array.elements.size * sizeof clone->array.elements.v[0]);
        forall(i, orig->array.elements) clone->array.elements.v[i] =
          tl_type_clone_impl(alloc, orig->array.elements.v[i], make_typevar, ctx, tvmap);
        break;

    case type_labelled_tuple:
        clone->array.elements.size = orig->array.elements.size;
        clone->array.elements.v =
          alloc_malloc(alloc, clone->array.elements.size * sizeof clone->array.elements.v[0]);
        forall(i, orig->array.elements) clone->array.elements.v[i] =
          tl_type_clone_impl(alloc, orig->array.elements.v[i], make_typevar, ctx, tvmap);

        clone->labelled_tuple.names.size = orig->labelled_tuple.names.size;
        clone->labelled_tuple.names.v =
          alloc_malloc(alloc, orig->labelled_tuple.names.size * sizeof clone->labelled_tuple.names.v[0]);
        forall(i, orig->labelled_tuple.names) clone->labelled_tuple.names.v[i] =
          alloc_strdup(alloc, orig->labelled_tuple.names.v[i]);
        break;

    case type_arrow:
        clone->arrow.left  = tl_type_clone_impl(alloc, orig->arrow.left, make_typevar, ctx, tvmap);
        clone->arrow.right = tl_type_clone_impl(alloc, orig->arrow.right, make_typevar, ctx, tvmap);
        break;

    case type_user:
        clone->user.name = alloc_strdup(alloc, orig->user.name);
        clone->user.labelled_tuple =
          tl_type_clone_impl(alloc, orig->user.labelled_tuple, make_typevar, ctx, tvmap);
        break;

    case type_type_var: {

        // assign the same new typevar for a corresponding original tvar.
        // eg, the type a -> a needs to be cloned to tv5 -> tv5, not tv5 -> tv6.

        u32 *found = map_get(*tvmap, &orig->type_var.val, sizeof orig->type_var.val);
        if (found) {
            clone->type_var.val = *found;
        } else {
            clone->type_var.val = make_typevar(ctx);
            map_set(tvmap, &orig->type_var.val, sizeof orig->type_var.val, &clone->type_var.val);
        }

    } break;

    case type_pointer:
        clone->pointer.target = tl_type_clone_impl(alloc, orig->pointer.target, make_typevar, ctx, tvmap);
        break;
    case type_any: break;
    }
    return clone;
}

tl_type *tl_type_clone(allocator *alloc, tl_type const *orig, tl_make_typevar_fun make_typevar, void *ctx) {

    hashmap *tvmap = map_create(alloc, sizeof(orig->type_var.val));
    tl_type *res   = tl_type_clone_impl(alloc, orig, make_typevar, ctx, &tvmap);
    map_destroy(&tvmap);
    return res;
}

int tl_type_is_prim(tl_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:            return 1;

    case type_user:
    case type_tuple:
    case type_labelled_tuple:
    case type_arrow:
    case type_type_var:       return 0;

    case type_pointer:        return tl_type_is_prim(self->pointer.target);
    }
    assert(0);
}

int tl_type_is_poly(tl_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:
    case type_user:   return 0;

    case type_tuple:  {
        struct tlt_tuple *v = tl_type_tup((tl_type *)self);
        for (u32 i = 0; i < v->elements.size; ++i)
            if (tl_type_is_poly(v->elements.v[i])) return 1;
        return 0;

    } break;

    case type_labelled_tuple: {
        struct tlt_labelled_tuple *v = tl_type_lt((tl_type *)self);
        for (u32 i = 0; i < v->fields.size; ++i)
            if (tl_type_is_poly(v->fields.v[i])) return 1;
        return 0;

    } break;

    case type_arrow:
        //
        return tl_type_is_poly(self->arrow.left) || tl_type_is_poly(self->arrow.right);

    case type_pointer:
        //
        return tl_type_is_poly(self->pointer.target);

    case type_type_var: return 1;
    }
    assert(0);
}

int tl_type_equal(tl_type const *left, tl_type const *right) {
    return tl_type_compare(left, right) == 0;
}

static int is_nil_or_empty_tuple(tl_type const *self) {
    if (self->tag == type_nil) return 1;
    if (self->tag == type_tuple || self->tag == type_labelled_tuple) return self->array.elements.size == 0;
    return 0;
}

int tl_type_compare(tl_type const *left, tl_type const *right) {
    // structural equality and total ordering for types

    if (left == right) return 0;

    if (is_nil_or_empty_tuple(left) && is_nil_or_empty_tuple(right))
        return 0; // tags are different but they're equal

    if (left->tag != right->tag) return left->tag < right->tag ? -1 : 1;

    switch (left->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:     return 0;

    case type_pointer: return tl_type_compare(left->pointer.target, right->pointer.target);

    case type_tuple:   {
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

u64 tl_type_hash(tl_type const *self) {
    return tl_type_hash_ext(self, 0);
}

u64 tl_type_hash_ext(tl_type const *self, int ignore_names) {
    u64 hash = hash64((byte *)&self->tag, sizeof self->tag);

    switch (self->tag) {

    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:
        //
        break;

    case type_pointer: {
        u64 target_hash = tl_type_hash_ext(self->pointer.target, ignore_names);
        hash            = hash64_combine(hash, (byte *)&target_hash, sizeof target_hash);
    } break;

    case type_tuple: {
        struct tlt_tuple const *v = tl_type_tup((tl_type *)self);
        for (u32 i = 0; i < v->elements.size; ++i) {
            u64 el_hash = tl_type_hash_ext(v->elements.v[i], ignore_names);
            hash        = hash64_combine(hash, (byte *)&el_hash, sizeof el_hash);
        }

    } break;

        //
    case type_labelled_tuple: {
        struct tlt_labelled_tuple const *v = tl_type_lt((tl_type *)self);

        for (u32 i = 0; i < v->fields.size; ++i) {
            u64 el_hash = tl_type_hash_ext(v->fields.v[i], ignore_names);
            hash        = hash64_combine(hash, (byte *)&el_hash, sizeof el_hash);
        }

        if (!ignore_names)
            for (u32 i = 0; i < v->names.size; ++i) {
                hash = hash64_combine(hash, (byte *)v->names.v[i], strlen(v->names.v[i]));
            }

    } break;

    case type_arrow: {
        struct tlt_arrow const *v          = tl_type_arrow((tl_type *)self);
        u64                     left_hash  = tl_type_hash_ext(v->left, ignore_names);
        u64                     right_hash = tl_type_hash_ext(v->right, ignore_names);
        hash                               = hash64_combine(hash, (byte *)&left_hash, sizeof left_hash);
        hash                               = hash64_combine(hash, (byte *)&right_hash, sizeof right_hash);
    } break;

    case type_user: {
        struct tlt_user const *v = tl_type_user((tl_type *)self);
        hash                     = hash64_combine(hash, (byte *)v->name, strlen(v->name));

        u64 lt_hash              = tl_type_hash(v->labelled_tuple);
        hash                     = hash64_combine(hash, (byte *)&lt_hash, sizeof lt_hash);
    } break;

    case type_type_var: {
        hash = hash64_combine(hash, (byte *)&self->type_var.val, sizeof self->type_var.val);
    }; break;
    }

    return hash;
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
    case type_any:    len = snprintf(buf, (size_t)sz, "%s", tl_type_tag_to_string(self->tag)); break;

    case type_pointer:

        len = snprintf(buf, sz, "*");
        if (buf && sz) len += tl_type_snprint(buf + len, sz - len, self->pointer.target);
        else len += tl_type_snprint(null, 0, self->pointer.target);

        break;

    case type_user: {
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

int tl_type_satisfies(tl_type const *requires, tl_type const *candidate) {
    // type variables are not satisfied by any type

    if (requires == candidate) return 1; // self-satisfied

    if (type_any == requires->tag || type_any == candidate->tag) return 1; // any satisfies all

    switch (requires->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string: return (requires->tag == candidate->tag);
    case type_pointer:
        return (requires->tag == candidate->tag) &&
               (tl_type_satisfies(requires->pointer.target, candidate->pointer.target));

    case type_labelled_tuple:
    case type_tuple:          {

        // labelled tuples satisfy plain tuples if their types match.
        if (type_tuple != candidate->tag && type_labelled_tuple != candidate->tag) return 0;

        struct tlt_array const *vreq  = tl_type_arr((tl_type *) requires),
                               *vcand = tl_type_arr((tl_type *)candidate);

        if (vreq->elements.size != vcand->elements.size) return 0;

        for (u32 i = 0; i < vreq->elements.size; ++i)
            if (!tl_type_satisfies(vreq->elements.v[i], vcand->elements.v[i])) return 0;

        return 1;
    }

    case type_arrow: {
        if (type_arrow != candidate->tag) return 0;

        struct tlt_arrow const *vreq  = tl_type_arrow((tl_type *) requires),
                               *vcand = tl_type_arrow((tl_type *)candidate);
        return candidate->tag == type_arrow && tl_type_satisfies(vreq->left, vcand->left) &&
               tl_type_satisfies(vreq->right, vcand->right);
    }

    case type_user: {
        if (type_arrow != candidate->tag) return 0;

        // same-named user types satisfy, though in that case they are expected to be the same identity
        assert(requires == candidate);
        return 0 == strcmp(requires->user.name, candidate->user.name);
    }

    case type_type_var:
        // are never satisfied
        return 0;

    case type_any: fatal("logic error");
    }
}

int tl_type_is_compatible(tl_type const *requires, tl_type const *candidate, int strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    if (tl_type_satisfies(requires, candidate)) return 1;
    if (strict) return 0;

    // if not strict, we are additionally satisfied when using type variables

    switch (requires->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_pointer:
        //
        return candidate->tag == type_type_var;

    case type_tuple: {
        if (type_type_var == candidate->tag) return 1;
        else if (type_tuple != candidate->tag && type_labelled_tuple != candidate->tag) return 0;

        struct tlt_array const *va = tl_type_arr((tl_type *) requires),
                               *vb = tl_type_arr((tl_type *)candidate);
        if (va->elements.size != vb->elements.size) return 0;

        forall(i, va->elements) {
            if (!tl_type_is_compatible(va->elements.v[i], vb->elements.v[i], strict)) return 0;
        }

        return 1;
    }

    case type_labelled_tuple: {
        if (type_type_var == candidate->tag) return 1;

        struct tlt_array const *varr        = tl_type_arr((tl_type *) requires),
                               *vbarr       = tl_type_arr((tl_type *)candidate);
        struct tlt_labelled_tuple const *va = tl_type_lt((tl_type *) requires),
                                        *vb = tl_type_lt((tl_type *)candidate);
        if (varr->elements.size != vbarr->elements.size) return 0;

        // regardless of typevars, names must match
        for (u32 i = 0; i < varr->elements.size; ++i) {
            if (0 != strcmp(va->names.v[i], vb->names.v[i])) return 0;
            if (!tl_type_is_compatible(varr->elements.v[i], vbarr->elements.v[i], strict)) return 0;
        }

        return 1;
    }

    case type_arrow:
        return candidate->tag == type_arrow &&
               tl_type_is_compatible(requires->arrow.left, candidate->arrow.left, strict) &&
               tl_type_is_compatible(requires->arrow.right, candidate->arrow.right, strict);

    case type_user:
        // user types are exclusively identified by reference
        return (requires == candidate);

    case type_type_var:
        // type variables match anything if not strict
        return 1;

    case type_any:
        //
        fatal("logic error");
    }
}

tl_type *tl_type_find_user_field_type(tl_type const *user_type, char const *field_name) {
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

tl_type *tl_type_find_labelled_field_type(tl_type const *lt_type, char const *field_name) {
    struct tlt_labelled_tuple *lt         = tl_type_lt((tl_type *)lt_type);
    tl_type                   *field_type = null;
    for (u32 i = 0; i < lt->names.size; ++i) {
        if (0 == strcmp(lt->names.v[i], field_name)) {
            field_type = lt->fields.v[i];
            break;
        }
    }
    return field_type;
}

int tl_type_contains(tl_type const *haystack, tl_type const *needle) {

    if (haystack == needle || tl_type_equal(haystack, needle)) return 1;

    switch (haystack->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_type_var:
    case type_any:            return 0;

    case type_pointer:        return tl_type_contains(haystack->pointer.target, needle);

    case type_tuple:
    case type_labelled_tuple: {
        struct tlt_array *v = tl_type_arr((tl_type *)haystack);
        for (u32 i = 0; i < v->elements.size; ++i) {
            if (tl_type_contains(v->elements.v[i], needle)) return 1;
        }
        return 0;
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

char const *tl_type_tag_to_string(tl_type_tag tag) {
    static char const *const strings[] = {TL_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}
