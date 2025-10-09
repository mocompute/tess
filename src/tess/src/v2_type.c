#include "v2_type.h"

#include "alloc.h"
#include "array.h"
#include "hashmap.h"
#include "str.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// -- type constructor --

tl_type_registry *tl_type_registry_create(allocator *alloc) {
    tl_type_registry *self = alloc_malloc(alloc, sizeof *self);
    self->alloc            = alloc;
    self->definitions      = map_create(self->alloc, sizeof(tl_type_constructor_def *), 64); // key: str
    self->instances =
      map_create(self->alloc, sizeof(tl_type_constructor_inst *), 64); // key: tl_type_con_def

    tl_type_constructor_def_create(self, S("Nil"), 0);
    tl_type_constructor_def_create(self, S("Int"), 0);
    tl_type_constructor_def_create(self, S("Bool"), 0);
    tl_type_constructor_def_create(self, S("Float"), 0);
    tl_type_constructor_def_create(self, S("String"), 0);

    tl_type_registry_instantiate(self, S("Nil"), null);
    tl_type_registry_instantiate(self, S("Int"), null);
    tl_type_registry_instantiate(self, S("Bool"), null);
    tl_type_registry_instantiate(self, S("Float"), null);
    tl_type_registry_instantiate(self, S("String"), null);

    return self;
}

tl_type_constructor_def *tl_type_constructor_def_create(tl_type_registry *self, str name, u32 arity) {
    tl_type_constructor_def *def = alloc_malloc(self->alloc, sizeof *def);
    def->name                    = str_copy(self->alloc, name);
    def->arity                   = arity;
    str_map_set_ptr(&self->definitions, def->name, def);
    return def;
}

typedef struct {
    // Note: key uses reference equality, not structural, so there may be duplication in the hashmap.
    str                name;
    tl_monotype const *args;
} registry_key;

tl_type_constructor_inst *tl_type_registry_instantiate(tl_type_registry *self, str name,
                                                       tl_monotype const *args) {
    tl_type_constructor_inst *inst = null;
    registry_key              key  = {.name = name, .args = args};
    if ((inst = map_get_ptr(self->instances, &key, sizeof key))) return inst;

    tl_type_constructor_def *def = str_map_get_ptr(self->definitions, name);
    if (!def) fatal("type cons name not found");
    if (tl_monotype_list_length(args) != def->arity) return null;

    inst       = alloc_malloc(self->alloc, sizeof *inst);
    inst->def  = def;
    inst->args = tl_monotype_list_copy(self->alloc, args);
    map_set_ptr(&self->instances, &key, sizeof key, inst);

    return inst;
}

tl_type_constructor_inst *tl_type_registry_get(tl_type_registry *self, str name, tl_monotype const *args) {
    // args may be null for empty list
    registry_key key = {.name = name, .args = args};
    return map_get_ptr(self->instances, &key, sizeof key);
}

tl_monotype *tl_type_registry_create_type(tl_type_registry *self, str name, tl_monotype *args) {
    // type must have been instantiated first
    tl_type_constructor_inst *inst = tl_type_registry_get(self, name, args);
    if (!inst) return null;

    tl_monotype *out = alloc_malloc(self->alloc, sizeof *out);

    // must clone constructed args list for type inference substitutions
    if (inst->args) {
        tl_type_constructor_inst *clone = alloc_malloc(self->alloc, sizeof *clone);
        memcpy(clone, inst, sizeof *clone);
        clone->args = tl_monotype_list_copy(self->alloc, clone->args);

        //
        *out = (tl_monotype){.cons = clone};
    } else {
        // use same inst for basic nullary constructed types
        *out = (tl_monotype){.cons = inst};
    }

    return out;
}

// -- type environment --

tl_type_env *tl_type_env_create(allocator *alloc, allocator *transient) {
    tl_type_env *self = alloc_malloc(alloc, sizeof *self);
    self->alloc       = alloc;
    self->transient   = transient;
    self->map         = map_create(self->alloc, sizeof(tl_polytype *), 64); // key: str

    return self;
}

void tl_type_env_insert(tl_type_env *self, str name, tl_polytype const *type) {
    tl_polytype *clone = tl_polytype_clone(self->alloc, type);
    str_map_set_ptr(&self->map, str_copy(self->alloc, name), clone);
}

void tl_type_env_insert_mono(tl_type_env *self, str name, tl_monotype const *type) {
    tl_polytype *clone = tl_polytype_absorb_mono(self->alloc, tl_monotype_clone(self->alloc, type));
    str_map_set_ptr(&self->map, str_copy(self->alloc, name), clone);
}

tl_polytype *tl_type_env_lookup(tl_type_env *self, str name) {
    return str_map_get_ptr(self->map, name);
}

// typedef void (*missing_fv_cb)(void *, str fun, str var);
int tl_type_env_check_missing_fvs(tl_type_env const *self, missing_fv_cb cb, void *user) {
    int              error = 0;

    hashmap_iterator iter  = {0};
    while (map_iter(self->map, &iter)) {
        str          name = *(str *)iter.key_ptr;
        tl_polytype *type = *(tl_polytype **)iter.data;

        str_sized    fvs  = tl_monotype_fvs(type->type);
        forall(i, fvs) {
            if (!str_map_contains(self->map, fvs.v[i])) {
                if (cb) cb(user, name, fvs.v[i]);
                ++error;
            }
        }
    }
    return error;
}

// -- polytype --

tl_polytype *tl_polytype_absorb_mono(allocator *alloc, tl_monotype *mono) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->quantifiers = (tl_type_variable_sized){0};
    self->type        = mono;
    return self;
}

tl_polytype *tl_polytype_create_qv(allocator *alloc, tl_type_variable qv) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->type        = tl_monotype_create_tv(alloc, qv);
    self->quantifiers =
      (tl_type_variable_sized){.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    self->quantifiers.v[0] = qv;
    return self;
}

tl_polytype *tl_polytype_create_fresh_qv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_qv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_clone(allocator *alloc, tl_polytype const *orig) {
    tl_polytype *clone = alloc_malloc(alloc, sizeof *clone);
    clone->quantifiers = orig->quantifiers;
    clone->type        = tl_monotype_clone(alloc, orig->type);
    return clone;
}

void tl_polytype_list_append(allocator *alloc, tl_polytype *lhs, tl_polytype *rhs) {
    if (rhs->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, lhs->quantifiers.size + rhs->quantifiers.size);
        array_push_many(arr, lhs->quantifiers.v, lhs->quantifiers.size);
        array_push_many(arr, rhs->quantifiers.v, rhs->quantifiers.size);
    }

    tl_monotype *tail = lhs->type;
    while (tail->next) tail = tail->next;
    tail->next = rhs->type;
}

static void replace_tv(tl_monotype *self, hashmap *map) {

    if (tl_monotype_is_tv(self)) {
        tl_type_variable *replace = map_get(map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
    } else {
        tl_monotype *hd = null;

        // type cons args
        if (self->cons) {
            hd = self->cons->args;
            while (hd) {
                replace_tv(hd, map);
                hd = hd->next;
            }
        }

        // list elements
        hd = self->next;
        while (hd) {
            replace_tv(hd, map);
            hd = hd->next;
        }
    }
}

tl_monotype *tl_polytype_instantiate(allocator *alloc, tl_polytype const *self, tl_type_subs *subs) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);
    if (!self->quantifiers.size) return fresh;

    hashmap *q_to_t = map_create(alloc, sizeof(tl_type_variable), 8);

    forall(i, self->quantifiers) {
        tl_type_variable tv = tl_type_subs_fresh(subs);
        map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &tv);
    }

    replace_tv(fresh, q_to_t);

    map_destroy(&q_to_t);
    return fresh;
}

static void generalize(tl_monotype *self, tl_type_variable_array *quant) {
    if (self->cons) {
        tl_monotype *arg = self->cons->args;
        while (arg) {
            generalize(arg, quant);
            arg = arg->next;
        }
    } else {
        array_set_insert(*quant, self->var);
    }

    tl_monotype *next = self->next;
    while (next) {
        generalize(next, quant);
        next = next->next;
    }
}

void tl_polytype_generalize(tl_polytype *self, tl_type_env const *env, tl_type_subs const *subs) {
    tl_polytype_substitute(env->alloc, self, subs);

    tl_type_variable_array quant = {.alloc = env->alloc};
    generalize(self->type, &quant);
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
    // leaks prior array, if any
}

// -- monotype --

static u32 list_length(tl_monotype const *head, u32 count) {
    if (!head) return count;
    return list_length(head->next, count + 1);
}

u32 tl_monotype_list_length(tl_monotype const *head) {
    return list_length(head, 0);
}

tl_monotype *tl_monotype_list_copy(allocator *alloc, tl_monotype const *head) {
    if (!head) return null;

    tl_monotype *copy = alloc_malloc(alloc, sizeof *copy);
    memcpy(copy, head, sizeof *copy);

    tl_monotype *hd = copy;
    while (hd->next) {
        tl_monotype *next = alloc_malloc(alloc, sizeof *next);
        memcpy(next, hd->next, sizeof *next);
        hd->next = next;
    }

    return copy;
}

tl_monotype *tl_monotype_list_last(tl_monotype *self) {
    if (!self->next) return self;
    return tl_monotype_list_last(self->next);
}

tl_monotype *tl_monotype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.var = tv};
    return self;
}

tl_monotype *tl_monotype_create_arrow(allocator *alloc, tl_monotype const *lhs, tl_monotype const *rhs) {
    tl_monotype *self = tl_monotype_clone(alloc, lhs);
    self->next        = tl_monotype_clone(alloc, rhs);
    return self;
}

tl_monotype *tl_monotype_create_cons(allocator *alloc, tl_type_constructor_inst *cons) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.cons = cons};
    return self;
}

tl_monotype *tl_monotype_clone(allocator *alloc, tl_monotype const *orig) {

    tl_monotype *clone = alloc_malloc(alloc, sizeof *clone);
    memcpy(clone, orig, sizeof *clone);
    if (orig->cons) {

        // copy the tl_type_constructor_inst struct
        clone->cons = alloc_malloc(alloc, sizeof *clone->cons);
        memcpy(clone->cons, orig->cons, sizeof *clone->cons);

        // clone the args list
        if (orig->cons->args) {
            clone->cons->args = tl_monotype_clone(alloc, orig->cons->args);
        }
    }

    if (orig->next) {
        clone->next = tl_monotype_clone(alloc, orig->next);
    }

    return clone;
}

int tl_monotype_is_tv(tl_monotype const *self) {
    return self && !self->next && !self->cons;
}

int tl_monotype_is_concrete(tl_monotype const *self) {
    return self && !self->next && self->cons;
}

int tl_monotype_is_arrow(tl_monotype const *self) {
    return self && self->next;
}

int tl_monotype_is_nil(tl_monotype const *self) {
    return self && self->cons && str_eq(self->cons->def->name, S("Nil"));
}

int tl_polytype_is_scheme(tl_polytype const *poly) {
    return poly->quantifiers.size != 0;
}

void tl_monotype_sort_fvs(tl_monotype *self) {
    if (!self->fvs) return;
    if (!self->fvs->size) return;
    qsort(self->fvs->v, self->fvs->size, sizeof self->fvs->v[0], str_cmp_v);
}

str_sized tl_monotype_fvs(tl_monotype const *self) {
    if (self->fvs) return *self->fvs;
    return (str_sized){0};
}

str tl_monotype_to_string(allocator *alloc, tl_monotype const *self) {

    str_build b = str_build_init(alloc, 64);

    if (self->fvs) {
        str_build_cat(&b, S("["));
        forall(i, *self->fvs) {
            str_build_cat(&b, self->fvs->v[i]);
        }
        str_build_cat(&b, S("] "));
    }

    if (self->cons) {
        str_build_cat(&b, self->cons->def->name);
        tl_monotype const *arg = self->cons->args;
        while (arg) {
            str_build_cat(&b, S(" "));
            str_build_cat(&b, tl_monotype_to_string(alloc, arg));
            arg = arg->next;
        }
    } else {
        char buf[64];
        snprintf(buf, sizeof buf, "t%u", self->var);
        str_build_cat(&b, str_init(alloc, buf));
    }

    if (self->next) {
        str_build_cat(&b, S(" -> "));
        while (self->next) {
            str_build_cat(&b, tl_monotype_to_string(alloc, self->next));
            self = self->next;
        }
    }

    return str_build_finish(&b);
}

str tl_polytype_to_string(allocator *alloc, tl_polytype const *self) {
    str_build b = str_build_init(alloc, 64);

    if (self->quantifiers.size) {
        str_build_cat(&b, S("forall"));
        forall(i, self->quantifiers) {
            char buf[64];
            snprintf(buf, sizeof buf, "q%u", self->quantifiers.v[i]);
            str_build_cat(&b, S(" "));
            str_build_cat(&b, str_init(alloc, buf));
        }
        str_build_cat(&b, S(". "));
    }

    str_build_cat(&b, tl_monotype_to_string(alloc, self->type));
    return str_build_finish(&b);
}

// -- substitutions --

tl_type_subs *tl_type_subs_create(allocator *alloc) {
    tl_type_subs *self = new (alloc, tl_type_subs);
    self->alloc        = alloc;
    array_reserve(*self, 1024);
    return self;
}

void tl_type_subs_destroy(allocator *alloc, tl_type_subs **p) {
    if (!p || !*p) return;
    array_free(**p);
    alloc_free(alloc, *p);
    *p = null;
}

tl_type_variable tl_type_subs_fresh(tl_type_subs *self) {
    tl_type_uf_node x = {.parent = self->size};
    array_push(*self, x);
    return x.parent;
}

static tl_type_variable uf_find(tl_type_subs *self, tl_type_variable tv) {
    assert(tv < self->size);
    if (self->v[tv].parent != tv) {
        // path compression
        self->v[tv].parent = uf_find(self, self->v[tv].parent);
    }
    return self->v[tv].parent;
}

static void uf_union(tl_type_subs *self, tl_type_variable tv1, tl_type_variable tv2) {
    tl_type_variable x = uf_find(self, tv1);
    tl_type_variable y = uf_find(self, tv2);
    assert(max(x, y) < self->size);

    if (x == y) return;

    // merge by rank
    if (self->v[x].rank < self->v[y].rank) swap(x, y);

    // make x the new root
    self->v[y].parent = x;
    if (self->v[x].rank == self->v[y].rank) self->v[x].rank++;
}

int unify_list(tl_type_subs *subs, tl_monotype const *left, tl_monotype const *right, type_error_cb_fun cb,
               void *user);

int tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype const *left, tl_monotype const *right,
                            type_error_cb_fun cb, void *user) {

    // resolve type variables, if possible
    if (tl_monotype_is_tv(left)) {
        tl_type_variable root     = uf_find(subs, left->var);
        tl_monotype     *resolved = subs->v[root].type;
        if (resolved) left = resolved;
    }
    if (tl_monotype_is_tv(right)) {
        tl_type_variable root     = uf_find(subs, right->var);
        tl_monotype     *resolved = subs->v[root].type;
        if (resolved) right = resolved;
    }

    // if both are still tvs, unify them
    if (tl_monotype_is_tv(left) && tl_monotype_is_tv(right)) {
        return tl_type_subs_unify(subs, left->var, right, cb, user);
    }

    // otherwise one is tv, the other is concrete or structural
    if (tl_monotype_is_tv(left)) return tl_type_subs_unify(subs, left->var, right, cb, user);
    if (tl_monotype_is_tv(right)) return tl_type_subs_unify(subs, right->var, left, cb, user);

    // both are concrete, must unify constructed types and/or arrows/lists
    if (left->cons || right->cons) {
        if (!left->cons || !right->cons) {
            if (cb) cb(user, left, right);
            return 1;
        }
        if (left->cons->def != right->cons->def) {
            if (cb) cb(user, left, right);
            return 1;
        }
        if (unify_list(subs, left->cons->args, right->cons->args, cb, user)) return 1;
    }

    if (unify_list(subs, left->next, right->next, cb, user)) return 1;

    return 0;
}

int unify_list(tl_type_subs *subs, tl_monotype const *left, tl_monotype const *right, type_error_cb_fun cb,
               void *user) {

    while (left || right) {
        if (!left || !right) {
            if (cb) cb(user, left, right);
            return 1;
        }
        if (tl_type_subs_unify_mono(subs, left, right, cb, user)) {
            if (cb) cb(user, left, right);
            return 1;
        }

        left  = left->next;
        right = right->next;
    }
    return 0;
}

int tl_type_subs_monotype_occurs(tl_type_subs *self, tl_type_variable tv, tl_monotype const *mono) {

    if (tl_monotype_is_tv(mono)) {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) return 1;
        tl_monotype *resolved = self->v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs(self, tv, resolved);
        return 0;
    } else {
        if (mono->cons) {
            tl_monotype const *hd = mono->cons->args;
            while (hd) {
                if (tl_type_subs_monotype_occurs(self, tv, hd)) return 1;
                hd = hd->next;
            }
        }
        if (mono->next) {
            tl_monotype const *hd = mono->next;
            while (hd) {
                if (tl_type_subs_monotype_occurs(self, tv, hd)) return 1;
                hd = hd->next;
            }
        }
        return 0;
    }
}

int tl_type_subs_unify(tl_type_subs *self, tl_type_variable tv, tl_monotype const *mono,
                       type_error_cb_fun cb, void *user) {
    tl_type_variable tv_root = uf_find(self, tv);

    // case 1: both are tvs
    if (tl_monotype_is_tv(mono)) {
        tl_type_variable mono_root = uf_find(self, mono->var);
        if (tv_root == mono_root) return 0; // already in same equivalence class

        tl_monotype *tv_type   = self->v[tv_root].type;
        tl_monotype *mono_type = self->v[mono_root].type;
        if (tv_type && mono_type) {
            // both are resolved: must unify
            if (!tl_type_subs_unify_mono(self, tv_type, mono_type, cb, user)) return 1;
        }

        // union the two classes
        uf_union(self, tv_root, mono_root);

        // preserve the resolved type, if any
        tl_type_variable union_root = uf_find(self, tv_root);
        if (tv_type) self->v[union_root].type = tv_type;
        else if (mono_type) self->v[union_root].type = mono_type;
        return 0;
    }

    // case 2: tv = concrete type
    if (tl_type_subs_monotype_occurs(self, tv, mono)) return 1;

    tl_monotype *tv_type = self->v[tv_root].type;
    if (tv_type) {
        // must unify
        return tl_type_subs_unify_mono(self, tv_type, mono, cb, user);
    }

    // store the type at the root
    self->v[tv_root].type = tl_monotype_clone(self->alloc, mono);
    return 0;
}

void tl_monotype_substitute(allocator *alloc, tl_monotype *self, tl_type_subs const *subs,
                            hashmap *exclude) {
    // exclude may be null

    if (tl_monotype_is_tv(self)) {
        if (exclude && hset_contains(exclude, &self->var, sizeof self->var)) return;
        tl_type_variable root = uf_find((tl_type_subs *)subs, self->var);
        if (exclude && hset_contains(exclude, &root, sizeof root)) return;

        tl_monotype *resolved = subs->v[root].type;
        if (resolved) {
            self = tl_monotype_clone(alloc, resolved);
            tl_monotype_substitute(alloc, self, subs, exclude);
        } else {
            // update to representative tv
            self->var = root;
        }

    } else {
        if (self->cons) {
            tl_monotype *hd = self->cons->args;
            while (hd) {
                tl_monotype_substitute(alloc, hd, subs, exclude);
                hd = hd->next;
            }
        }

        if (self->next) {
            tl_monotype *hd = self->next;
            while (hd) {
                tl_monotype_substitute(alloc, hd, subs, exclude);
                hd = hd->next;
            }
        }
    }
}

void tl_polytype_substitute_ext(allocator *alloc, tl_polytype *self, tl_type_subs const *subs,
                                hashmap **exclude) {
    map_reset(*exclude);

    if (self->quantifiers.size) {
        forall(i, self->quantifiers) {
            hset_insert(exclude, &self->quantifiers.v[i], sizeof(tl_type_variable));
        }
    }

    tl_monotype_substitute(alloc, self->type, subs, *exclude);
}

void tl_polytype_substitute(allocator *alloc, tl_polytype *self, tl_type_subs const *subs) {
    hashmap *exclude = null;
    if (self->quantifiers.size) {
        exclude = map_create(alloc, sizeof(tl_type_variable), 8);
        forall(i, self->quantifiers) {
            hset_insert(&exclude, &self->quantifiers.v[i], sizeof(tl_type_variable));
        }
    }

    tl_monotype_substitute(alloc, self->type, subs, exclude);

    if (exclude) map_destroy(&exclude);
}

tl_polytype tl_polytype_wrap(tl_monotype *mono) {
    return (tl_polytype){.type = mono};
}

void tl_type_subs_apply(tl_type_subs *subs, tl_type_env *env) {

    hashmap         *exclude = map_create(subs->alloc, sizeof(tl_type_variable), 8);

    hashmap_iterator iter    = {0};
    while (map_iter(env->map, &iter)) {
        tl_polytype *poly = *(tl_polytype **)iter.key_ptr;
        tl_polytype_substitute_ext(subs->alloc, poly, subs, &exclude);
    }

    map_destroy(&exclude);
}

// --------------------------------------------------------------------------

#if 0

// -- monotype --

// void tl_monotype_union_fv(tl_monotype *dst, tl_monotype const *src) {
//     assert(tl_arrow == dst->tag);
//     if (tl_arrow != src->tag) return;
//     forall(i, src->arrow.fvs) array_set_insert(dst->arrow.fvs, src->arrow.fvs.v[i]);
// }


// -- type --


//

// -- tl_type_subs --

//

void tl_type_env_subs_apply(allocator *alloc, tl_type_env *env, tl_type_subs const *subs) {
    forall(i, env->types) {
        tl_type_v2_apply_subs(alloc, &env->types.v[i], subs);
    }
}

str tl_type_variable_to_string(allocator *alloc, tl_type_variable const *self) {
    char buf[64];
    snprintf(buf, sizeof buf, "t%u", *self);
    return str_init(alloc, buf);
}

str tl_type_quantifier_to_string(allocator *alloc, tl_type_quantifier const *self) {
    char buf[64];
    snprintf(buf, sizeof buf, "q%u", *self);
    return str_init(alloc, buf);
}

str tl_type_constructor_inst_to_string(allocator *alloc, tl_type_constructor_inst const *self) {
    str_build b = str_build_init(alloc, 64);
    str_build_cat(&b, self->name);

    forall(i, self->args) {
        str_build_cat(&b, S(" "));

        // be friendly with bump allocators which can free the last allocated block
        str mono = tl_monotype_to_string(alloc, self->args.v[i]);
        str_build_cat(&b, mono);
        str_deinit(alloc, &mono);
    }

    return str_build_finish(&b);
}

str tl_type_arrow_to_string(allocator *alloc, tl_type_v2_arrow const *self) {
    assert(self && (void *)self != self->lhs && (void *)self != self->rhs);
    str_build b = str_build_init(alloc, 64);
    {
        str left = tl_monotype_to_string(alloc, self->lhs);
        str_build_cat(&b, left);
        str_deinit(alloc, &left);
    }
    str_build_cat(&b, S(" -> "));
    {
        str right = tl_monotype_to_string(alloc, self->rhs);
        str_build_cat(&b, right);
        str_deinit(alloc, &right);
    }

    if (self->fvs.size) {
        str_build_cat(&b, S(" ["));
        forall(i, self->fvs) {
            str_build_cat(&b, self->fvs.v[i]);
            if (i < self->fvs.size - 1) str_build_cat(&b, S(" "));
        }
        str_build_cat(&b, S("]"));
    }

    return str_build_finish(&b);
}
#endif

str tl_type_subs_to_string(allocator *alloc, tl_type_subs const *self) {
    return str_copy(alloc, S("not implemented"));
    (void)self;
}

// -- env --

void tl_type_env_log(tl_type_env *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->map, &iter)) {
        str                name     = *(str *)iter.key_ptr;
        tl_polytype const *type     = *(tl_polytype **)iter.data;
        str                type_str = tl_polytype_to_string(self->transient, type);

        fprintf(stderr, "%.*s : %.*s\n", str_ilen(name), str_buf(&name), str_ilen(type_str),
                str_buf(&type_str));
    }
}

//

void tl_type_subs_log(allocator *alloc, tl_type_subs *self) {
    hashmap               *seen        = hset_create(alloc, 128);
    tl_type_variable_array equiv_class = {.alloc = alloc};

    forall(i, *self) {
        tl_type_variable root = uf_find(self, i);
        if (hset_contains(seen, &root, sizeof root)) continue;
        hset_insert(&seen, &root, sizeof root);

        equiv_class.size = 0;
        forall(j, *self) {
            if (uf_find(self, j) == root) array_push(equiv_class, j);
        }

        fprintf(stderr, "  {");
        forall(j, equiv_class) {
            fprintf(stderr, "t%u", equiv_class.v[j]);
            if (j + 1 < equiv_class.size) fprintf(stderr, ", ");
        }
        fprintf(stderr, "}");

        tl_monotype *type = self->v[root].type;
        if (type) {
            fprintf(stderr, " = ");
            str s = tl_monotype_to_string(alloc, type);
            fprintf(stderr, "%.*s", str_ilen(s), str_buf(&s));
        }

        fprintf(stderr, "\n");
    }

    array_free(equiv_class);
    hset_destroy(&seen);
}
