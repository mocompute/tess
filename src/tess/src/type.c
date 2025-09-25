#include "type.h"
#include "alloc.h"
#include "hash.h"
#include "hashmap.h"
#include "string_t.h"

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

tl_type *tl_type_create_arrow(allocator *alloc, tl_type *left, tl_type *right) {
    tl_type *self              = alloc_struct(alloc, self);
    self->tag                  = type_arrow;
    self->arrow.left           = left;
    self->arrow.right          = right;
    self->arrow.free_variables = (tl_free_variable_sized){0};
    self->arrow.flags          = 0;

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
        clone->arrow.flags = orig->arrow.flags;
        clone->arrow.free_variables = orig->arrow.free_variables; // shallow copy
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

    case type_any:
    case type_ellipsis:
        //
        break;
    }
    return clone;
}

tl_type *tl_type_clone(allocator *alloc, tl_type const *orig, tl_make_typevar_fun make_typevar, void *ctx) {

    hashmap *tvmap = map_create(alloc, sizeof(orig->type_var.val));
    tl_type *res   = tl_type_clone_impl(alloc, orig, make_typevar, ctx, &tvmap);
    map_destroy(&tvmap);
    return res;
}

tl_type *tl_type_clone_shallow(allocator *alloc, tl_type const *orig) {
    // TODO: duplicated with clone_impl
    if (!orig) return null;

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
          tl_type_clone_shallow(alloc, orig->array.elements.v[i]);
        break;

    case type_labelled_tuple:
        clone->array.elements.size = orig->array.elements.size;
        clone->array.elements.v =
          alloc_malloc(alloc, clone->array.elements.size * sizeof clone->array.elements.v[0]);
        forall(i, orig->array.elements) clone->array.elements.v[i] =
          tl_type_clone_shallow(alloc, orig->array.elements.v[i]);

        clone->labelled_tuple.names.size = orig->labelled_tuple.names.size;
        clone->labelled_tuple.names.v =
          alloc_malloc(alloc, orig->labelled_tuple.names.size * sizeof clone->labelled_tuple.names.v[0]);
        forall(i, orig->labelled_tuple.names) clone->labelled_tuple.names.v[i] =
          alloc_strdup(alloc, orig->labelled_tuple.names.v[i]);
        break;

    case type_arrow:
        clone->arrow.left           = tl_type_clone_shallow(alloc, orig->arrow.left);
        clone->arrow.right          = tl_type_clone_shallow(alloc, orig->arrow.right);
        clone->arrow.flags          = orig->arrow.flags;
        clone->arrow.free_variables = orig->arrow.free_variables; // shallow copy
        break;

    case type_user:
        clone->user.name           = alloc_strdup(alloc, orig->user.name);
        clone->user.labelled_tuple = tl_type_clone_shallow(alloc, orig->user.labelled_tuple);
        break;

    case type_type_var: {

        clone->type_var.val = orig->type_var.val;

    } break;

    case type_pointer: clone->pointer.target = tl_type_clone_shallow(alloc, orig->pointer.target); break;

    case type_any:
    case type_ellipsis:
        //
        break;
    }
    return clone;
}

int tl_type_is_prim(tl_type const *self) {
    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:
    case type_ellipsis:       return 1;

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
    case type_user:
    case type_ellipsis: return 0;

    case type_tuple:    {
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
    case type_any:
    case type_ellipsis:
        //
        return 0;

    case type_pointer:
        //
        return tl_type_compare(left->pointer.target, right->pointer.target);

    case type_tuple: {
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
        if ((res = tl_type_compare(vleft->left, vright->left))) return res;
        if ((res = tl_type_compare(vleft->right, vright->right))) return res;
        if ((res = tl_free_variable_array_cmp(vleft->free_variables, vright->free_variables))) return res;
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
    return tl_type_hash_ext(self, 0, 0);
}

u64 tl_type_hash_ext(tl_type const *self, int ignore_names, int ignore_free_variable_types) {
    u64 hash = hash64((byte *)&self->tag, sizeof self->tag);

    switch (self->tag) {

    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:
    case type_ellipsis:
        //
        break;

    case type_pointer: {
        u64 target_hash = tl_type_hash_ext(self->pointer.target, ignore_names, ignore_free_variable_types);
        hash            = hash64_combine(hash, (byte *)&target_hash, sizeof target_hash);
    } break;

    case type_tuple: {
        struct tlt_tuple const *v = tl_type_tup((tl_type *)self);
        for (u32 i = 0; i < v->elements.size; ++i) {
            u64 el_hash = tl_type_hash_ext(v->elements.v[i], ignore_names, ignore_free_variable_types);
            hash        = hash64_combine(hash, (byte *)&el_hash, sizeof el_hash);
        }

    } break;

        //
    case type_labelled_tuple: {
        struct tlt_labelled_tuple const *v = tl_type_lt((tl_type *)self);

        for (u32 i = 0; i < v->fields.size; ++i) {
            u64 el_hash = tl_type_hash_ext(v->fields.v[i], ignore_names, ignore_free_variable_types);
            hash        = hash64_combine(hash, (byte *)&el_hash, sizeof el_hash);
        }

        if (!ignore_names)
            for (u32 i = 0; i < v->names.size; ++i) {
                hash = hash64_combine(hash, (byte *)v->names.v[i], strlen(v->names.v[i]));
            }

    } break;

    case type_arrow: {
        struct tlt_arrow const *v = tl_type_arrow((tl_type *)self);
        u64 left_hash             = tl_type_hash_ext(v->left, ignore_names, ignore_free_variable_types);
        u64 right_hash            = tl_type_hash_ext(v->right, ignore_names, ignore_names);
        u64 free_variable_hash =
          tl_free_variable_array_hash64(v->free_variables, ignore_free_variable_types);
        hash = hash64_combine(hash, (byte *)&left_hash, sizeof left_hash);
        hash = hash64_combine(hash, (byte *)&right_hash, sizeof right_hash);
        if (!ignore_free_variable_types)
            hash = hash64_combine(hash, (byte *)&free_variable_hash, sizeof free_variable_hash);

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
    int est = sz ? 1 : 0; // to avoid duplication with snprintf

    if (null == self) return snprintf(buf, (size_t)sz, "[null]");

    switch (self->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_any:
    case type_ellipsis:
        //
        len = snprintf(buf, (size_t)sz, "%s", tl_type_tag_to_string(self->tag));
        break;

    case type_pointer:

        len = snprintf(buf, sz, "*");
        len += tl_type_snprint(buf + len * est, sz - len * est, self->pointer.target);

        break;

    case type_user: {
        struct tlt_user const *v = tl_type_user((tl_type *)self);

        len                      = 0;
        len += snprintf(buf, (size_t)sz, "(%s ", v->name);
        len += tl_type_snprint(buf + len * est, sz - len * est, v->labelled_tuple);
        len += snprintf(buf + len * est, (size_t)(sz - len * est), ")");

    } break;

    case type_tuple: {
        struct tlt_tuple const *v = tl_type_tup((tl_type *)self);

        len                       = 0;

        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < v->elements.size; ++i) {
            len += tl_type_snprint(buf + len * est, sz - len * est, v->elements.v[i]);
            len += snprintf(buf + len * est, (size_t)(sz - len * est), ", ");
        }

        len += snprintf(buf + len * est, (size_t)(sz - len * est), ")");

    } break;

    case type_labelled_tuple: {
        struct tlt_labelled_tuple const *v = tl_type_lt((tl_type *)self);

        len                                = 0;
        len += snprintf(buf, (size_t)sz, "(");

        for (size_t i = 0; i < v->fields.size; ++i) {
            len += snprintf(buf + len * est, (size_t)(sz - len * est), "%s : ", v->names.v[i]);
            len += tl_type_snprint(buf + len * est, sz - len * est, v->fields.v[i]);
            len += snprintf(buf + len * est, (size_t)(sz - len * est), ", ");
        }

        len += snprintf(buf + len * est, (size_t)(sz - len * est), ")");

    } break;

    case type_arrow: {
        struct tlt_arrow const *v = tl_type_arrow((tl_type *)self);

        len                       = 0;

        len += tl_type_snprint(buf, sz, v->left);
        len += snprintf(buf + len * est, (size_t)(sz - len * est), " -> ");
        len += tl_type_snprint(buf + len * est, sz - len * est, v->right);
        len += snprintf(buf + len * est, (size_t)(sz - len * est), " [fv: %u ", v->free_variables.size);
        forall(i, v->free_variables) {
            char type_str[128];
            tl_type_snprint(type_str, sizeof type_str, v->free_variables.v[i].type);
            len += snprintf(buf + len * est, (size_t)(sz - len * est), "%s (%s)",
                            string_t_str(&v->free_variables.v[i].name), type_str

            );
            if (i < v->free_variables.size - 1)
                len += snprintf(buf + len * est, (size_t)(sz - len * est), ", ");
        }
        len += snprintf(buf + len * est, (size_t)(sz - len * est), "]");

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

static int is_singular(tl_type const *self) {
    switch (self->tag) {
    case type_tuple:
    case type_labelled_tuple: return (self->array.elements.size == 0);

    case type_nil:            // nil is not singular, it is emptiness
    case type_ellipsis:       return 0;

    case type_bool:
    case type_int:
    case type_float:
    case type_string:
    case type_arrow:
    case type_user:
    case type_type_var:
    case type_pointer:
    case type_any:            return 1; break;
    }
}

static int find_ellipsis(tl_type const *self) {
    if (type_tuple != self->tag && type_labelled_tuple != self->tag) return -1;
    forall(i, self->array.elements) {
        if (type_ellipsis == self->array.elements.v[i]->tag) return (int)i;
    }
    return -1;
}

int tl_type_satisfies(tl_type const *requires, tl_type const *candidate) {
    // type variables are not satisfied by any type. any is satisfied
    // by any singular type, but not tuples with 1 or more element.
    // ellipsis can be used as the final element of a tuple, and is
    // satisfied by any number including zero candidate elements, of
    // any type.

    if (requires == candidate) return 1; // self-satisfied

    if (type_any == requires->tag && is_singular(candidate)) return 1;
    if (type_any == candidate->tag && is_singular(requires)) return 1;

    // nil and empty tuple are equivalent

    switch (requires->tag) {
    case type_nil:
        if (type_nil == candidate->tag) return 1;
        if ((type_tuple == candidate->tag || type_labelled_tuple == candidate->tag) &&
            0 == candidate->array.elements.size)
            return 1;
        return 0;

    case type_bool:
    case type_int:
    case type_float:
    case type_string: return (requires->tag == candidate->tag);
    case type_pointer:
        return (requires->tag == candidate->tag) &&
               (tl_type_satisfies(requires->pointer.target, candidate->pointer.target));

    case type_labelled_tuple:
    case type_tuple:          {

        if (0 == requires->array.elements.size && type_nil == candidate->tag) return 1;

        // labelled tuples satisfy plain tuples if their types match.
        if (type_tuple != candidate->tag && type_labelled_tuple != candidate->tag) return 0;

        struct tlt_array const *vreq  = tl_type_arr((tl_type *) requires),
                               *vcand = tl_type_arr((tl_type *)candidate);

        int req_ell = find_ellipsis(requires), cand_ell = find_ellipsis(candidate);

        // common case is no ellipsis => rule out tuples of different size
        if (req_ell == -1 && cand_ell == -1 && vreq->elements.size != vcand->elements.size) return 0;
        if (req_ell < 0) req_ell = (int)vreq->elements.size;    // TODO integer overflow
        if (cand_ell < 0) cand_ell = (int)vcand->elements.size; // TODO integer overflow

        // anything after requires' ellipsis automatically satisfies,
        // so we don't loop past it.
        for (int i = 0; i < req_ell; ++i)
            // anything after candidate's ellipsis automatically
            // satisfies, so we don't test it
            if (i < cand_ell && !tl_type_satisfies(vreq->elements.v[i], vcand->elements.v[i])) return 0;

        return 1;
    }

    case type_arrow: {
        if (type_arrow != candidate->tag) return 0;

        struct tlt_arrow const *vreq  = tl_type_arrow((tl_type *) requires),
                               *vcand = tl_type_arrow((tl_type *)candidate);
        int arrows_ok =
          tl_type_satisfies(vreq->left, vcand->left) && tl_type_satisfies(vreq->right, vcand->right);

        // ignore free_variables
        return arrows_ok;
    }

    case type_user: {
        if (type_user != candidate->tag) return 0;

        return 0 == strcmp(requires->user.name, candidate->user.name);
    }

    case type_type_var:
        // are never satisfied
        return 0;

    case type_any:
        // counterpart is not singular type
        return 0;

    case type_ellipsis: fatal("logic error");
    }
}

int tl_type_is_compatible(tl_type const *requires, tl_type const *candidate, int strict) {
    // strict => do not accept typevars for compatibility. This is
    // used when looking for a specialised function, which should
    // exclude any generic functions.

    if (tl_type_satisfies(requires, candidate)) return 1;
    if (strict) return 0;

    // if not strict, we are additionally satisfied when using type variables
    if (type_type_var == requires->tag || type_type_var == candidate->tag) return 1;

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

    case type_arrow: {
        int arrows_ok = candidate->tag == type_arrow &&
                        tl_type_is_compatible(requires->arrow.left, candidate->arrow.left, strict) &&
                        tl_type_is_compatible(requires->arrow.right, candidate->arrow.right, strict);

        // ignore free variables when compatibility testing
        return arrows_ok;
    }
    case type_user:
        //
        return (requires->tag == candidate->tag);

    case type_type_var:
        // type variables match anything if not strict
        return 1;

    case type_any:
        //
        return is_singular(candidate);

    case type_ellipsis:
        //
        return 1;
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

tl_type *tl_type_get_arrow(tl_type *self) {
    if (type_arrow == self->tag) return self;
    if (type_pointer == self->tag && type_arrow == self->pointer.target->tag) return self;
    return null;
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
    case type_any:
    case type_ellipsis:       return 0;

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
        if (tl_type_contains(v->left, needle) || tl_type_contains(v->right, needle)) return 1;
        forall(i, v->free_variables) if (tl_type_contains(v->free_variables.v[i].type, needle)) return 1;
        return 0;
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

int tl_free_variable_array_cmp(tl_free_variable_sized lhs, tl_free_variable_sized rhs) {

    if (lhs.size != rhs.size) return lhs.size < rhs.size ? -1 : 1;

    forall(i, lhs) {
        int res;
        if ((res = tl_free_variable_cmp(&lhs.v[i], &rhs.v[i]))) return res;
    }
    return 0;
}

u64 tl_free_variable_array_hash64(tl_free_variable_sized arr, int ignore_types) {

    // NOTE: ignores special_hash
    u64 hash = 0;
    forall(i, arr) {
        u64 str = string_t_hash64(&arr.v[i].name);
        u64 ty  = tl_type_hash(arr.v[i].type);
        hash    = hash64_combine(hash, (void *)&str, sizeof str);
        if (!ignore_types) hash = hash64_combine(hash, (void *)&ty, sizeof ty);
    }
    return hash;
}

int tl_free_variable_array_contains(tl_free_variable_sized haystack, tl_free_variable_sized needle) {
    forall(i, needle) {
        forall(j, haystack) if (0 == tl_free_variable_cmp(&needle.v[i], &haystack.v[j])) goto found;
        goto not_found; // finished inner loop without finding

    found:;
    }

    return 1; // finished outer loop without error

not_found:
    return 0;
}

int tl_free_variable_array_contains_one(tl_free_variable_sized haystack, tl_free_variable needle) {
    return tl_free_variable_array_contains(haystack, (tl_free_variable_sized){.v = &needle, .size = 1});
}

int tl_free_variable_cmp(tl_free_variable const *lhs, tl_free_variable const *rhs) {
    // if either special_hash is zero, don't use it for comparison
    if (lhs->special_hash && rhs->special_hash) {
        if (lhs->special_hash != rhs->special_hash) return lhs->special_hash < rhs->special_hash ? -1 : 1;
    }

    int res;
    if ((res = string_t_cmp(&lhs->name, &rhs->name))) return res;
    return tl_type_compare(lhs->type, rhs->type);
}

int tl_free_variable_cmpv(void const *lhs, void const *rhs) {
    return tl_free_variable_cmp((tl_free_variable const *)lhs, (tl_free_variable const *)rhs);
}

void tl_free_variable_array_merge(tl_free_variable_array *dst, tl_free_variable_sized src) {
    forall(i, src) {
        if (!tl_free_variable_array_contains_one((tl_free_variable_sized)sized_all(*dst), src.v[i]))
            array_push(*dst, &src.v[i]);
    }
}
