#include "v2_type.h"

#include "alloc.h"
#include "array.h"
#include "dbg.h"
#include "hash.h"
#include "hashmap.h"
#include "str.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>

// -- monotype --

tl_monotype tl_monotype_init_nil() {
    return (tl_monotype){.tag = tl_nil};
}

tl_monotype tl_monotype_init_tv(tl_type_variable tv) {
    return (tl_monotype){.tag = tl_var, .var = tv};
}

tl_monotype tl_monotype_init_quant(tl_type_quantifier q) {
    return (tl_monotype){.tag = tl_quant, .var = q};
}

tl_monotype tl_monotype_init_arrow(tl_type_v2_arrow arrow) {
    return (tl_monotype){.tag = tl_arrow, .arrow = arrow};
}

tl_monotype tl_monotype_alloc_arrow(allocator *alloc, tl_monotype left, tl_monotype right) {
    tl_monotype *pleft  = tl_monotype_create(alloc, left);
    tl_monotype *pright = tl_monotype_create(alloc, right);
    tl_monotype  arrow  = tl_monotype_init_arrow((tl_type_v2_arrow){.lhs = pleft, .rhs = pright});
    return arrow;
}

void tl_monotype_dealloc(allocator *alloc, tl_monotype *self) {
    switch (self->tag) {
    case tl_cons:
        array_free(self->cons.args);
        str_deinit(alloc, &self->cons.name);
        break;
    case tl_arrow:
        alloc_free(alloc, self->arrow.lhs);
        alloc_free(alloc, self->arrow.rhs);
        array_free(self->arrow.fvs);
        break;

    case tl_var:
    case tl_quant:
    case tl_nil:   break;
    }

    alloc_invalidate(self);
}

tl_monotype tl_monotype_init_constructor_inst(tl_type_constructor_inst cons) {
    return (tl_monotype){.tag = tl_cons, .cons = cons};
}

tl_monotype *tl_monotype_create(allocator *alloc, tl_monotype init) {
    tl_monotype *self = new (alloc, tl_monotype);
    *self             = init;
    return self;
}

void tl_monotype_destroy(allocator *alloc, tl_monotype **p) {
    // shallow destroy; use an arena at least for arrow types if not for everything
    alloc_free(alloc, *p);
    *p = null;
}

int tl_monotype_eq(tl_monotype lhs, tl_monotype rhs) {
    if (lhs.tag != rhs.tag) return 0;
    switch (lhs.tag) {
    case tl_nil: return 1;
    case tl_cons:
        if (!str_eq(lhs.cons.name, rhs.cons.name)) return 0;
        if (lhs.cons.args.size != rhs.cons.args.size) return 0;
        forall(i, lhs.cons.args) {
            if (!tl_monotype_eq(lhs.cons.args.v[i], rhs.cons.args.v[i])) return 0;
        }
        return 1;
    case tl_var:   return lhs.var == rhs.var;
    case tl_quant: return lhs.quant == rhs.quant;
    case tl_arrow: return tl_monotype_eq(*lhs.arrow.lhs, *rhs.arrow.rhs); break;
    }
}

int tl_monotype_is_nil(tl_monotype const *mono) {
    return tl_nil == mono->tag;
}

int tl_monotype_is_monomorphic(tl_monotype const *self) {
    switch (self->tag) {
    case tl_nil: return 1;
    case tl_cons:
        forall(i, self->cons.args) {
            if (!tl_monotype_is_monomorphic(&self->cons.args.v[i])) return 0;
        }
        return 1;
    case tl_var:
    case tl_quant: return 0;
    case tl_arrow:
        return tl_monotype_is_monomorphic(self->arrow.lhs) && tl_monotype_is_monomorphic(self->arrow.rhs);
    }
}

int tl_monotype_occurs(tl_monotype lhs, tl_monotype rhs) {
    // return 1 if either side has a type variable that occurs on the other side
    switch (lhs.tag) {

    case tl_nil:
        //
        return 0;

    case tl_cons:
        forall(i, lhs.cons.args) if (tl_monotype_occurs(lhs.cons.args.v[i], rhs)) return 1;
        return 0;

    case tl_var:
        if (tl_var == rhs.tag) return lhs.var == rhs.var;
        else if (tl_quant == rhs.tag || tl_nil == rhs.tag) return 0;
        return tl_monotype_occurs(rhs, lhs);

    case tl_quant:
        if (tl_quant == rhs.tag) return lhs.quant == rhs.quant;
        else if (tl_var == rhs.tag || tl_nil == rhs.tag) return 0;
        return tl_monotype_occurs(rhs, lhs);

    case tl_arrow:
        return tl_monotype_occurs(*lhs.arrow.lhs, rhs) || tl_monotype_occurs(*lhs.arrow.rhs, rhs);
    }
}

u64 tl_monotype_hash64(tl_monotype self) {
    u64 hash = hash64(&self.tag, sizeof self.tag);
    switch (self.tag) {

    case tl_nil: break;

    case tl_cons:
        hash = str_hash64_combine(hash, self.cons.name);
        forall(i, self.cons.args) {
            u64 h = tl_monotype_hash64(self.cons.args.v[i]);
            hash  = hash64_combine(hash, &h, sizeof h);
        }
        break;

    case tl_var:   hash = hash64_combine(hash, &self.var, sizeof self.var); break;
    case tl_quant: hash = hash64_combine(hash, &self.quant, sizeof self.quant); break;

    case tl_arrow: {
        u64 h = tl_monotype_hash64(*self.arrow.lhs);
        hash  = hash64_combine(hash, &h, sizeof h);
        h     = tl_monotype_hash64(*self.arrow.rhs);
        hash  = hash64_combine(hash, &h, sizeof h);
    } break;
    }
    return hash;
}

void tl_monotype_union_fv(tl_monotype *dst, tl_monotype src) {
    assert(tl_arrow == dst->tag);
    if (tl_arrow != src.tag) return;
    forall(i, src.arrow.fvs) array_set_insert(dst->arrow.fvs, src.arrow.fvs.v[i]);
}

void tl_type_v2_arrow_sort_fvs(tl_type_v2_arrow *self) {
    if (!self->fvs.size) return;
    qsort(self->fvs.v, self->fvs.size, sizeof self->fvs.v[0], str_cmp_v);
}

// -- type --

tl_type_v2 tl_type_init_mono(tl_monotype mono) {
    return (tl_type_v2){.tag = tl_mono, .mono = mono};
}

tl_type_v2 tl_type_init_scheme(tl_type_scheme scheme) {
    return (tl_type_v2){.tag = tl_scheme, .scheme = scheme};
}

tl_type_v2 *tl_type_alloc_mono(allocator *alloc, tl_monotype mono) {
    tl_type_v2 *out = new (alloc, tl_type_v2);
    *out            = tl_type_init_mono(mono);
    return out;
}

tl_type_v2 *tl_type_alloc_type(allocator *alloc, tl_type_v2 const *type) {
    tl_type_v2 *out = new (alloc, tl_type_v2);
    *out            = *type;
    return out;
}

tl_monotype tl_monotype_clone(allocator *alloc, tl_monotype orig) {
    tl_monotype clone = {0};
    clone.tag         = orig.tag;
    switch (orig.tag) {
    case tl_nil: break;
    case tl_cons:
        clone.cons.name = str_copy(alloc, orig.cons.name);
        clone.cons.args = (tl_monotype_array){.alloc = alloc};
        array_copy(clone.cons.args, orig.cons.args);
        forall(i, clone.cons.args) {
            // cons args are monotype
            clone.cons.args.v[i] = tl_monotype_clone(alloc, clone.cons.args.v[i]);
        }
        break;
    case tl_var:   clone.var = orig.var; break;
    case tl_quant: clone.quant = orig.quant; break;

    case tl_arrow:
        clone.arrow.lhs  = new (alloc, tl_monotype);
        clone.arrow.rhs  = new (alloc, tl_monotype);
        clone.arrow.fvs  = (str_array){.alloc = alloc};
        *clone.arrow.lhs = tl_monotype_clone(alloc, *orig.arrow.lhs);
        *clone.arrow.rhs = tl_monotype_clone(alloc, *orig.arrow.rhs);
        array_copy(clone.arrow.fvs, orig.arrow.fvs);
        break;
    }
    return clone;
}

tl_type_v2 tl_type_v2_clone(allocator *alloc, tl_type_v2 const *orig) {
    tl_type_v2 clone = {0};
    switch (orig->tag) {
    case tl_mono:

        clone.tag  = tl_mono;
        clone.mono = tl_monotype_clone(alloc, orig->mono);
        break;
    case tl_scheme:
        clone.tag                = tl_scheme;

        clone.scheme.type        = tl_monotype_clone(alloc, orig->scheme.type);
        clone.scheme.quantifiers = (tl_type_quantifier_array){.alloc = alloc};
        array_copy(clone.scheme.quantifiers, orig->scheme.quantifiers);
        // tl_type_quantifier does not need to be cloned
        break;
    }
    return clone;
}

int tl_type_v2_is_arrow(tl_type_v2 const *self) {
    return (tl_scheme == self->tag && tl_arrow == self->scheme.type.tag) ||
           (tl_mono == self->tag && tl_arrow == self->mono.tag);
}

int tl_type_v2_is_scheme(tl_type_v2 const *self) {
    return tl_scheme == self->tag;
}

int tl_type_v2_is_mono(tl_type_v2 const *self) {
    return tl_mono == self->tag;
}

int tl_type_v2_is_monomorphic(tl_type_v2 const *self) {
    if (tl_type_v2_is_scheme(self)) return 0;
    return tl_monotype_is_monomorphic(&self->mono);
}

str_sized tl_type_v2_free_variables(tl_type_v2 const *self) {
    str_sized out = {0};
    switch (self->tag) {
    case tl_mono:
        if (tl_arrow == self->mono.tag) {
            out.size = self->mono.arrow.fvs.size;
            out.v    = self->mono.arrow.fvs.v;
        }
        break;

    case tl_scheme:
        if (tl_arrow == self->scheme.type.tag) {
            out.size = self->scheme.type.arrow.fvs.size;
            out.v    = self->scheme.type.arrow.fvs.v;
        }
        break;
    }

    return out;
}

//

// -- tl_type_subs --

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

int tl_monotype_unify(allocator *alloc, tl_type_subs *subs, tl_monotype *left, tl_monotype *right,
                      type_error_cb_fun cb, void *user) {

    // resolve type variables, if possible
    if (tl_var == left->tag) {
        tl_type_variable root     = uf_find(subs, left->var);
        tl_monotype     *resolved = subs->v[root].type;
        if (resolved) left = resolved;
    }
    if (tl_var == right->tag) {
        tl_type_variable root     = uf_find(subs, right->var);
        tl_monotype     *resolved = subs->v[root].type;
        if (resolved) right = resolved;
    }

    // if both are still tvs, unify them
    if (tl_var == left->tag && tl_var == right->tag) {
        return tl_type_subs_unify(alloc, subs, left->var, right, cb, user);
    }

    // otherwise one is tv, the other is concrete or structural
    if (tl_var == left->tag) return tl_type_subs_unify(alloc, subs, left->var, right, cb, user);
    if (tl_var == right->tag) return tl_type_subs_unify(alloc, subs, right->var, left, cb, user);

    // both are concrete, must unify structures:
    if (left->tag != right->tag) {
        if (cb) cb(user, left, right);
        return 1;
    }

    switch (left->tag) {
    case tl_nil: return 0;

    case tl_cons:
        if (!str_eq(left->cons.name, right->cons.name)) {
            return 1;
        }
        if (left->cons.args.size != right->cons.args.size) {
            return 1;
        }
        forall(i, left->cons.args) {
            if (tl_monotype_unify(alloc, subs, &left->cons.args.v[i], &right->cons.args.v[i], cb, user)) {
                if (cb) cb(user, &left->cons.args.v[i], &right->cons.args.v[i]);
                return 1;
            }
        }
        return 0;

    case tl_arrow:
        if (tl_monotype_unify(alloc, subs, left->arrow.lhs, right->arrow.lhs, cb, user)) {
            if (cb) cb(user, left->arrow.lhs, right->arrow.lhs);
            return 1;
        }

        if (tl_monotype_unify(alloc, subs, left->arrow.rhs, right->arrow.rhs, cb, user)) {
            if (cb) cb(user, left->arrow.rhs, right->arrow.rhs);
            return 1;
        }
        return 0;

    case tl_quant:
    case tl_var:   fatal("unreachable");
    }
}

int tl_type_subs_monotype_occurs(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono) {
    switch (mono->tag) {
    case tl_nil: return 0;
    case tl_var: {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) return 1;
        tl_monotype *resolved = self->v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs(self, tv, resolved);
        return 0;
    } break;

    case tl_cons:
        forall(i, mono->cons.args) {
            if (tl_type_subs_monotype_occurs(self, tv, &mono->cons.args.v[i])) return 1;
        }
        return 0;

    case tl_arrow:
        return tl_type_subs_monotype_occurs(self, tv, mono->arrow.lhs) ||
               tl_type_subs_monotype_occurs(self, tv, mono->arrow.rhs);
    case tl_quant: fatal("unreachable");
    }
}

int tl_type_subs_unify(allocator *alloc, tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                       type_error_cb_fun cb, void *user) {
    tl_type_variable tv_root = uf_find(self, tv);

    // case 1: both are tvs
    if (tl_var == mono->tag) {
        tl_type_variable mono_root = uf_find(self, mono->var);
        if (tv_root == mono_root) return 0; // already in same equivalence class

        tl_monotype *tv_type   = self->v[tv_root].type;
        tl_monotype *mono_type = self->v[mono_root].type;
        if (tv_type && mono_type) {
            // both are resolved: must unify
            if (!tl_monotype_unify(alloc, self, tv_type, mono_type, cb, user)) return 1;
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
    if (tl_monotype_occurs(tl_monotype_init_tv(tv_root), *mono)) return 1;

    tl_monotype *tv_type = self->v[tv_root].type;
    if (tv_type) {
        // must unify
        return tl_monotype_unify(alloc, self, tv_type, mono, cb, user);
    }

    // store the type at the root
    self->v[tv_root].type  = new (alloc, tl_monotype);
    *self->v[tv_root].type = tl_monotype_clone(alloc, *mono);
    return 0;
}

static void tl_monotype_substitute(tl_monotype *self, tl_type_subs const *subs) {
    switch (self->tag) {
    case tl_cons: {
        forall(i, self->cons.args) {
            tl_monotype_substitute(&self->cons.args.v[i], subs);
        }
    } break;

    case tl_var: {
        tl_type_variable root     = uf_find((tl_type_subs *)subs, self->var);
        tl_monotype     *resolved = subs->v[root].type;
        if (resolved) *self = *resolved;

    } break;

    case tl_arrow: {
        tl_monotype_substitute(self->arrow.lhs, subs);
        tl_monotype_substitute(self->arrow.rhs, subs);
    } break;

    case tl_quant:
    case tl_nil:   break;
    }
}

void tl_type_v2_apply_subs(tl_type_v2 *self, tl_type_subs const *subs) {

    // for type schemes, we can treat its type as a monotype for the
    // purpose of substitution, because quantified variables (which
    // are not substitutable) are a different C type than unquantified
    // type variables.

    if (tl_mono == self->tag) return tl_monotype_substitute(&self->mono, subs);
    else return tl_monotype_substitute(&self->scheme.type, subs);
}

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

//

void tl_type_env_subs_apply(tl_type_env *env, tl_type_subs const *subs) {
    forall(i, env->types) {
        tl_type_v2_apply_subs(&env->types.v[i], subs);
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
        str mono = tl_monotype_to_string(alloc, &self->args.v[i]);
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

str tl_monotype_to_string(allocator *alloc, tl_monotype const *self) {
    switch (self->tag) {
    case tl_cons:  return tl_type_constructor_inst_to_string(alloc, &self->cons);
    case tl_var:   return tl_type_variable_to_string(alloc, &self->var);
    case tl_quant: return tl_type_quantifier_to_string(alloc, &self->var);
    case tl_arrow: return tl_type_arrow_to_string(alloc, &self->arrow);
    case tl_nil:   return str_copy(alloc, S("()"));
    }
}

str tl_type_scheme_to_string(allocator *alloc, tl_type_scheme const *self) {
    // forall t0 t1 ... . (dot) (type)
    str_build b = str_build_init(alloc, 64);
    str_build_cat(&b, S("forall"));
    forall(i, self->quantifiers) {
        str_build_cat(&b, S(" "));
        str tv = tl_type_quantifier_to_string(alloc, &self->quantifiers.v[i]);
        str_build_cat(&b, tv);
        str_deinit(alloc, &tv);
    }

    str_build_cat(&b, S(" . "));

    str mono = tl_monotype_to_string(alloc, &self->type);
    str_build_cat(&b, mono);
    str_deinit(alloc, &mono);
    return str_build_finish(&b);
}

str tl_type_v2_to_string(allocator *alloc, tl_type_v2 const *self) {
    switch (self->tag) {
    case tl_mono:   return tl_monotype_to_string(alloc, &self->mono);
    case tl_scheme: return tl_type_scheme_to_string(alloc, &self->scheme);
    }
}

str tl_type_subs_to_string(allocator *alloc, tl_type_subs const *self) {
    return str_copy(alloc, S("not implemented"));
    (void)self;
}

// -- env --

static void add_type_cons(tl_type_env *self, tl_type_constructor_inst inst) {
    tl_type_env_add_mono(self, inst.name, tl_monotype_init_constructor_inst(inst));
}

static void make_builtin_type_constructors(tl_type_env *self) {

    tl_type_constructor_inst inst = {0};
    //
    inst.name = S("Nil");
    add_type_cons(self, inst);
    inst.name = S("Int");
    add_type_cons(self, inst);
    inst.name = S("Bool");
    add_type_cons(self, inst);
    inst.name = S("Float");
    add_type_cons(self, inst);
    inst.name = S("String");
    add_type_cons(self, inst);

    for (u32 i = 0; i < self->names.size; ++i) {
        str_map_set(&self->index, self->names.v[i], &i);
    }
}

tl_type_env *tl_type_env_create(allocator *alloc) {
    tl_type_env *self = new (alloc, tl_type_env);

    self->names       = (str_array){.alloc = alloc};
    self->types       = (tl_type_v2_array){.alloc = alloc};
    self->index       = map_create(alloc, sizeof(u32), 128);

    make_builtin_type_constructors(self);

    return self;
}

nodiscard tl_type_env *tl_type_env_copy(tl_type_env const *src) {

    allocator   *alloc = src->names.alloc;

    tl_type_env *self  = new (alloc, tl_type_env);
    self->index        = map_copy(self->index);
    self->names        = (str_array){.alloc = alloc};
    self->types        = (tl_type_v2_array){.alloc = alloc};
    array_copy(self->names, src->names);
    array_copy(self->types, src->types);

    return self;
}

void tl_type_env_destroy(allocator *alloc, tl_type_env **p) {
    if (!p || !*p) return;
    map_destroy(&(*p)->index);
    array_free((*p)->names);
    array_free((*p)->types);
    alloc_free(alloc, *p);
    *p = null;
}

u32 tl_type_env_add(tl_type_env *self, str name, tl_type_v2 const *type) {
    assert(!str_is_empty(name));
    u32 *found = str_map_get(self->index, name);
    if (found) {
        self->types.v[*found] = *type;
        return *found;
    }

    array_push(self->names, name);
    array_push(self->types, *type);
    assert(self->names.size == self->types.size);

    u32 loc = self->names.size - 1;
    str_map_set(&self->index, name, &loc);
    return loc;
}

u32 tl_type_env_add_mono(tl_type_env *self, str name, tl_monotype mono) {
    tl_type_v2 type = tl_type_init_mono(mono);
    return tl_type_env_add(self, name, &type);
}

tl_type_v2 *tl_type_env_lookup(tl_type_env *self, str name) {
    u32 *found = str_map_get(self->index, name);
    assert(!found || *found < self->types.size);
    return found ? &self->types.v[*found] : null;
}

int tl_type_env_find_tv(tl_type_env const *self, tl_type_variable tv, u32 *out_index) {
    tl_monotype tv_mono = tl_monotype_init_tv(tv);

    forall(i, self->types) {
        tl_type_v2 *type = &self->types.v[i];
        switch (type->tag) {
        case tl_mono:
            if (tl_monotype_occurs(tv_mono, type->mono)) {
                if (out_index) *out_index = i;
                return 1;
            }
            break;

        case tl_scheme:
            if (tl_monotype_occurs(tv_mono, type->scheme.type)) {
                if (out_index) *out_index = i;
                return 1;
            }
            break;
        }
    }
    return 0;
}

void tl_type_env_erase(tl_type_env *self, u32 idx) {
    assert(idx < self->names.size);

    str name = self->names.v[idx];
    array_erase(self->names, idx);
    array_erase(self->types, idx);
    str_map_erase(self->index, name);
}

void tl_type_env_reindex(tl_type_env *self) {
    map_reset(self->index);
    forall(i, self->names) {
        str_map_set(&self->index, self->names.v[i], &i);
    }
}

// -- context --

tl_type_context tl_type_context_empty() {
    return (tl_type_context){
      .next_quant = 0,
    };
}

tl_monotype tl_type_context_new_quantifier(tl_type_context *self) {
    return tl_monotype_init_quant((tl_type_quantifier)self->next_quant++);
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
