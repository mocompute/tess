#include "v2_type.h"

#include "alloc.h"
#include "array.h"
#include "hash.h"
#include "hashmap.h"
#include "str.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>

static void log(tl_type_env const *self, char const *restrict fmt, ...);

// -- type constructor --

tl_type_registry *tl_type_registry_create(allocator *alloc, tl_type_subs *subs) {
    tl_type_registry *self = alloc_malloc(alloc, sizeof *self);
    self->alloc            = alloc;
    self->subs             = subs;
    self->definitions      = map_create(self->alloc, sizeof(tl_type_constructor_def *), 64); // key: str
    self->instances        = map_create(self->alloc, sizeof(tl_monotype *), 64); // key: registry_key

    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};
    tl_monotype_sized      empty_mt = {0};

    tl_type_constructor_def_create(self, S("Nil"), empty_tv, empty, empty_mt);
    tl_type_constructor_def_create(self, S("Int"), empty_tv, empty, empty_mt);
    tl_type_constructor_def_create(self, S("Bool"), empty_tv, empty, empty_mt);
    tl_type_constructor_def_create(self, S("Float"), empty_tv, empty, empty_mt);
    tl_type_constructor_def_create(self, S("String"), empty_tv, empty, empty_mt);

    tl_type_variable_sized unary = {.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    unary.v[0]                   = tl_type_subs_fresh(self->subs);
    tl_monotype const *tv_type   = tl_monotype_create_tv(alloc, unary.v[0]);

    tl_monotype_array  mt_arr    = {.alloc = alloc};
    array_push(mt_arr, tv_type);
    tl_type_constructor_def_create(self, S("Ptr"), unary, empty, (tl_monotype_sized)sized_all(mt_arr));

    empty_mt  = (tl_monotype_sized){0};
    str blank = str_empty();
    tl_type_registry_specialize(self, S("Nil"), blank, empty_mt);
    tl_type_registry_specialize(self, S("Int"), blank, empty_mt);
    tl_type_registry_specialize(self, S("Bool"), blank, empty_mt);
    tl_type_registry_specialize(self, S("Float"), blank, empty_mt);
    tl_type_registry_specialize(self, S("String"), blank, empty_mt);

    return self;
}

tl_polytype const *tl_type_constructor_def_create(tl_type_registry *self, str name,
                                                  tl_type_variable_sized type_variables,
                                                  str_sized field_names, tl_monotype_sized field_types) {

    tl_type_constructor_def *def   = alloc_malloc(self->alloc, sizeof *def);
    def->name                      = str_copy(self->alloc, name);
    def->field_names               = field_names;

    tl_type_constructor_inst *inst = alloc_malloc(self->alloc, sizeof *inst);
    inst->def                      = def;
    inst->args                     = field_types;
    inst->special_name             = str_empty();

    tl_monotype *mono              = tl_monotype_create_cons(self->alloc, inst);
    tl_polytype *poly              = tl_polytype_absorb_mono(self->alloc, mono);
    poly->quantifiers              = type_variables;

    str_map_set_ptr(&self->definitions, def->name, poly);
    return poly;
}

typedef struct {
    u64 name_hash;
    u64 args_hash;

} registry_key;

tl_monotype const *tl_type_registry_instantiate(tl_type_registry *self, str name) {
    tl_monotype const *type = null;

    tl_polytype       *poly = str_map_get_ptr(self->definitions, name);
    if (!poly) return null;

    type = tl_polytype_instantiate(self->alloc, poly, self->subs);

    return type;
}

tl_monotype const *tl_type_registry_instantiate_with(tl_type_registry *self, str name,
                                                     tl_monotype_sized args) {
    tl_monotype const *type = null;

    tl_polytype       *poly = str_map_get_ptr(self->definitions, name);
    if (!poly) return null;

    u32 arity = poly->quantifiers.size;
    if (args.size != arity) return null;

    type = tl_polytype_instantiate_with(self->alloc, poly, args);

    return type;
}

tl_monotype const *tl_type_registry_get_cached_instance(tl_type_registry *self, str name,
                                                        tl_monotype_sized args) {
    registry_key key = {.name_hash = str_hash64(name), .args_hash = tl_monotype_sized_hash64(0, args)};
    return map_get_ptr(self->instances, &key, sizeof key);
}

tl_monotype const *tl_type_registry_specialize(tl_type_registry *self, str name, str special_name,
                                               tl_monotype_sized args) {
    tl_monotype const *type = null;
    registry_key key = {.name_hash = str_hash64(name), .args_hash = tl_monotype_sized_hash64(0, args)};
    if ((type = map_get_ptr(self->instances, &key, sizeof key))) return type;

    tl_polytype *poly = str_map_get_ptr(self->definitions, name);
    if (!poly) fatal("type cons name not found");

    u32 arity = poly->type->cons_inst->args.size;
    if (args.size != arity) return null;

    type = tl_polytype_specialize_cons(self->alloc, poly, args, self, special_name);
    map_set_ptr(&self->instances, &key, sizeof key, type);

    return type;
}

int tl_type_registry_exists(tl_type_registry *self, str name) {
    return str_map_contains(self->definitions, name);
}

tl_monotype const *tl_type_registry_nil(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Nil"));
}
tl_monotype const *tl_type_registry_int(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Int"));
}
tl_monotype const *tl_type_registry_float(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Float"));
}
tl_monotype const *tl_type_registry_bool(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Bool"));
}
tl_monotype const *tl_type_registry_string(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("String"));
}

tl_monotype const *tl_type_registry_ptr(tl_type_registry *self, tl_monotype const *arg) {
    tl_monotype const *out =
      tl_type_registry_instantiate_with(self, S("Ptr"), (tl_monotype_sized){.size = 1, .v = &arg});
    assert(out);
    return out;
}

// -- type environment --

tl_type_env *tl_type_env_create(allocator *alloc, allocator *transient) {
    tl_type_env *self = alloc_malloc(alloc, sizeof *self);
    self->alloc       = alloc;
    self->transient   = transient;
    self->map         = map_create(self->alloc, sizeof(tl_polytype *), 64); // key: str
    self->verbose     = 0;

    return self;
}

void tl_type_env_insert(tl_type_env *self, str name, tl_polytype const *type) {
    str type_str = tl_polytype_to_string(self->transient, type);
    log(self, "tl_type_env_insert %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));

    tl_polytype const *clone = tl_polytype_clone(self->alloc, type);
    str_map_set_ptr(&self->map, str_copy(self->alloc, name), clone);
}

void tl_type_env_insert_mono(tl_type_env *self, str name, tl_monotype const *type) {
    tl_polytype const *clone = tl_polytype_absorb_mono(self->alloc, tl_monotype_clone(self->alloc, type));
    str                type_str = tl_polytype_to_string(self->transient, clone);
    log(self, "tl_type_env_insert %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));
    str_map_set_ptr(&self->map, str_copy(self->alloc, name), clone);
}

tl_polytype const *tl_type_env_lookup(tl_type_env *self, str name) {
    return str_map_get_ptr(self->map, name);
}

int tl_type_env_check_missing_fvs(tl_type_env const *self, missing_fv_cb cb, void *user) {
    int              error = 0;

    hashmap_iterator iter  = {0};
    while (map_iter(self->map, &iter)) {
        str          name = str_init_n(self->transient, iter.key_ptr, iter.key_size);
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

tl_polytype *tl_polytype_absorb_mono(allocator *alloc, tl_monotype const *mono) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->quantifiers = (tl_type_variable_sized){0};
    self->type        = mono;
    return self;
}

tl_polytype const *tl_polytype_create_qv(allocator *alloc, tl_type_variable qv) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->type        = tl_monotype_create_tv(alloc, qv);
    self->quantifiers =
      (tl_type_variable_sized){.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    self->quantifiers.v[0] = qv;
    return self;
}

tl_polytype const *tl_polytype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype const *mono = tl_monotype_create_tv(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype const *tl_polytype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype const *mono = tl_monotype_create_weak(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype const *tl_polytype_create_fresh_qv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_qv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype const *tl_polytype_create_fresh_tv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_tv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype const *tl_polytype_clone(allocator *alloc, tl_polytype const *orig) {
    tl_polytype *clone = alloc_malloc(alloc, sizeof *clone);
    clone->quantifiers = orig->quantifiers;
    clone->type        = tl_monotype_clone(alloc, orig->type);
    return clone;
}

void tl_polytype_list_append(allocator *alloc, tl_polytype *lhs, tl_polytype const *rhs) {

    if (rhs->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, lhs->quantifiers.size + rhs->quantifiers.size);
        array_push_many(arr, lhs->quantifiers.v, lhs->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, rhs->quantifiers) array_set_insert(arr, rhs->quantifiers.v[i]);
        alloc_free(alloc, lhs->quantifiers.v);
        lhs->quantifiers.size = arr.size;
        lhs->quantifiers.v    = arr.v;
    }

    tl_monotype       *left  = (tl_monotype *)lhs->type; // const cast
    tl_monotype const *right = rhs->type;
    assert(tl_list == left->tag);

    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, left->list.xs.size);
    array_push_many(arr, left->list.xs.v, left->list.xs.size);

    switch (right->tag) {
    case tl_var:
    case tl_weak:
    case tl_cons_inst: array_push(arr, right); break;
    case tl_list:
    case tl_tuple:     array_push_many(arr, right->list.xs.v, right->list.xs.size); break;
    }

    array_shrink(arr);
    alloc_free(alloc, left->list.xs.v);
    left->list.xs = (tl_monotype_sized)sized_all(arr);
}

void tl_polytype_merge_quantifiers(allocator *alloc, tl_polytype *self, tl_polytype const *in) {
    if (in->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, self->quantifiers.size + in->quantifiers.size);
        array_push_many(arr, self->quantifiers.v, self->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, in->quantifiers) array_set_insert(arr, in->quantifiers.v[i]);
        array_shrink(arr);

        alloc_free(alloc, self->quantifiers.v);
        self->quantifiers.size = arr.size;
        self->quantifiers.v    = arr.v;
    }
}

static void replace_tv(tl_monotype *self, hashmap *map) {
    if (!self) return;

    switch (self->tag) {
    case tl_var: {
        tl_type_variable *replace = map_get(map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
    } break;

    case tl_weak:
        // weak type variables are not generalizable and therefore do
        // not participate in instantiation
        break;

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) replace_tv((tl_monotype *)arr.v[i], map); // const cast
    } break;
    }
}

static void replace_tv_mono(tl_monotype *self, hashmap *map) {
    if (!self) return;

    switch (self->tag) {
    case tl_var: {
        tl_monotype const *replace = map_get_ptr(map, &self->var, sizeof self->var);
        if (replace) *self = *replace;
    } break;

    case tl_weak:
        // weak type variables are not generalizable and therefore do
        // not participate in instantiation
        break;

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) replace_tv_mono((tl_monotype *)arr.v[i], map); // const cast
    } break;
    }
}

tl_monotype const *tl_polytype_instantiate(allocator *alloc, tl_polytype const *self, tl_type_subs *subs) {
    tl_monotype *fresh = (tl_monotype *)tl_monotype_clone(alloc, self->type); // const cast
    if (!self->quantifiers.size) return fresh;

    hashmap *q_to_t = map_create(alloc, sizeof(tl_type_variable), 8);

    forall(i, self->quantifiers) {
        // make a fresh variable for each quantified type variable
        tl_type_variable tv = tl_type_subs_fresh(subs);
        map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &tv);
    }

    replace_tv(fresh, q_to_t);

    map_destroy(&q_to_t);
    return fresh;
}

tl_monotype const *tl_polytype_instantiate_with(allocator *alloc, tl_polytype const *self,
                                                tl_monotype_sized tvs) {
    tl_monotype *fresh = (tl_monotype *)tl_monotype_clone(alloc, self->type); // const cast
    if (!self->quantifiers.size) return fresh;
    if (self->quantifiers.size != tvs.size) fatal("logic error");

    hashmap *q_to_t = map_create(alloc, sizeof(tl_monotype *), tvs.size);

    forall(i, self->quantifiers) {
        map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &tvs.v[i]);
    }

    replace_tv_mono(fresh, q_to_t);

    map_destroy(&q_to_t);
    return fresh;
}

tl_monotype const *tl_polytype_specialize(allocator *alloc, tl_polytype const *self,
                                          tl_monotype_sized args) {
    tl_monotype *fresh = (tl_monotype *)tl_monotype_clone(alloc, self->type); // const cast

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized const *inst = &fresh->cons_inst->args;
        if (args.size != inst->size) fatal("logic error");
        forall(i, *inst) inst->v[i] = args.v[i];
    }

    return fresh;
}

tl_monotype const *tl_polytype_specialize_cons(allocator *alloc, tl_polytype const *self,
                                               tl_monotype_sized args, tl_type_registry *registry,
                                               str special_name) {
    tl_monotype *fresh = (tl_monotype *)tl_monotype_clone(alloc, self->type); // const cast

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized const *inst = &fresh->cons_inst->args;
        if (args.size != inst->size) fatal("logic error");

        // specialise cons arguments using registry
        forall(i, args) {
            if (tl_monotype_is_inst(args.v[i])) {
                str                name      = args.v[i]->cons_inst->def->name;
                tl_monotype_sized  inst_args = args.v[i]->cons_inst->args;
                tl_monotype const *replace =
                  tl_type_registry_get_cached_instance(registry, name, inst_args);
                if (!replace) return null;
                args.v[i] = replace;
            }
        }

        forall(i, *inst) inst->v[i]    = args.v[i];
        fresh->cons_inst->special_name = str_copy(alloc, special_name);
    }

    return fresh;
}

static void generalize(tl_monotype *self, tl_type_variable_array *quant) {
    if (!self) return;

    switch (self->tag) {
    case tl_var: array_set_insert(*quant, self->var); break;

    case tl_weak:
        // weak type variables are not generalizeable
        break;

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) generalize((tl_monotype *)arr.v[i], quant); // const cast

    } break;
    }
}

void tl_polytype_generalize(tl_polytype *self, tl_type_env const *env, tl_type_subs *subs) {
    tl_polytype_substitute(env->alloc, self, subs);

    tl_type_variable_array quant = {.alloc = env->transient}; // transient
    generalize((tl_monotype *)self->type, &quant);            // const cast
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
    // leaks prior array, if any

    // instantiate to get fresh vars, then generalise again using the fresh vars
    self->type = tl_polytype_instantiate(env->alloc, self, subs);
    quant      = (tl_type_variable_array){.alloc = env->alloc};
    generalize((tl_monotype *)self->type, &quant); // const cast
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
}

tl_monotype const *tl_polytype_concrete(tl_polytype const *self) {
    if (!tl_polytype_is_concrete(self)) fatal("runtime error");
    return self->type;
}

// -- monotype --

tl_monotype *tl_monotype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_var, .var = tv};
    return self;
}

tl_monotype *tl_monotype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak, .var = tv};
    return self;
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak(self->alloc, tv);
}

tl_monotype *tl_monotype_create_arrow(allocator *alloc, tl_monotype const *lhs, tl_monotype const *rhs) {
    tl_monotype const *left  = tl_monotype_clone(alloc, lhs);
    tl_monotype const *right = tl_monotype_clone(alloc, rhs);
    tl_monotype_array  arr   = {.alloc = alloc};
    array_reserve(arr, 2);
    array_push(arr, left);
    array_push(arr, right);
    return tl_monotype_create_list(alloc, (tl_monotype_sized)sized_all(arr));
}

tl_monotype *tl_monotype_create_list(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_list, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_tuple(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_tuple, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_cons(allocator *alloc, tl_type_constructor_inst *cons) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_cons_inst, .cons_inst = cons};
    return self;
}

tl_monotype *tl_monotype_clone(allocator *alloc, tl_monotype const *orig) {

    if (!orig) return null;
    tl_monotype *clone = alloc_malloc(alloc, sizeof *clone);

    switch (orig->tag) {
    case tl_var:  *clone = (tl_monotype){.tag = tl_var, .var = orig->var}; return clone;
    case tl_weak: *clone = (tl_monotype){.tag = tl_weak, .var = orig->var}; return clone;

    case tl_cons_inst:
        // copy the tl_type_constructor_inst struct
        *clone =
          (tl_monotype){.tag = tl_cons_inst, .cons_inst = alloc_malloc(alloc, sizeof *clone->cons_inst)};
        *clone->cons_inst = *orig->cons_inst; // const cast

        // clone the args list
        clone->cons_inst->args         = tl_monotype_sized_clone(alloc, orig->cons_inst->args);
        clone->cons_inst->special_name = str_copy(alloc, orig->cons_inst->special_name);

        break;

    case tl_list:
    case tl_tuple:
        *clone =
          (tl_monotype){.tag = orig->tag, .list = {.xs = tl_monotype_sized_clone(alloc, orig->list.xs)}};
        clone->list.fvs = orig->list.fvs; // shallow copy
        break;
    }

    return clone;
}

int tl_monotype_is_concrete(tl_monotype const *self) {
    if (!self) return 0;
    switch (self->tag) {
    case tl_var:
    case tl_weak:      return 0;
    case tl_cons_inst: return 1;
    case tl_list:
    case tl_tuple:     {
        forall(i, self->list.xs) if (!tl_monotype_is_concrete(self->list.xs.v[i])) return 0;
        return 1;
    }
    }
}

int tl_monotype_is_concrete_no_arrow(tl_monotype const *self) {
    return self && tl_cons_inst == self->tag;
}

int tl_monotype_is_arrow(tl_monotype const *self) {
    return self && tl_list == self->tag;
}

int tl_monotype_is_nil(tl_monotype const *self) {
    return self && tl_cons_inst == self->tag && self->cons_inst &&
           str_eq(self->cons_inst->def->name, S("Nil"));
}

int tl_monotype_is_list(tl_monotype const *self) {
    return self && tl_list == self->tag;
}

int tl_monotype_is_inst(tl_monotype const *self) {
    return self && tl_cons_inst == self->tag;
}

int tl_monotype_is_tuple(tl_monotype const *self) {
    return self && tl_tuple == self->tag;
}

int tl_monotype_is_ptr(tl_monotype const *self) {
    return self && tl_cons_inst == self->tag && self->cons_inst->def &&
           str_eq(self->cons_inst->def->name, S("Ptr"));
}

int tl_polytype_is_scheme(tl_polytype const *poly) {
    return poly->quantifiers.size != 0;
}

int tl_polytype_is_concrete(tl_polytype const *self) {
    return !tl_polytype_is_scheme(self) && tl_monotype_is_concrete(self->type);
}

int tl_polytype_is_type_constructor(tl_polytype const *self) {
    return tl_monotype_is_inst(self->type);
}

void tl_monotype_sort_fvs(tl_monotype *self) {
    if (tl_list != self->tag) return;
    if (!self->list.fvs.size) return;
    qsort(self->list.fvs.v, self->list.fvs.size, sizeof self->list.fvs.v[0], str_cmp_v);
}

str_sized tl_monotype_fvs(tl_monotype const *self) {
    if (tl_list != self->tag) return (str_sized){0};
    return self->list.fvs;
}

void tl_monotype_absorb_fvs(tl_monotype *self, str_sized fvs) {
    if (tl_list != self->tag) fatal("logic error");
    self->list.fvs = fvs;
}

u64 tl_type_constructor_def_hash64(tl_type_constructor_def const *self) {
    u64 hash = str_hash64(self->name);
    hash     = str_array_hash64(hash, self->field_names);
    return hash;
}

u64 tl_monotype_hash64(tl_monotype const *self) {
    u64 hash = hash64(&self->tag, sizeof self->tag);
    switch (self->tag) {
    case tl_var:
    case tl_weak:      hash = hash64_combine(hash, &self->var, sizeof self->var); break;

    case tl_cons_inst: {
        u64 def_hash = tl_type_constructor_def_hash64(self->cons_inst->def);
        hash         = hash64_combine(hash, &def_hash, sizeof def_hash);
        hash         = tl_monotype_sized_hash64(hash, self->cons_inst->args);
        // Important: do not include special_name as part of hash, because specialize_user_type uses
        // unspecialised name + hash to de-duplicate
    } break;

    case tl_list:
    case tl_tuple: {
        hash = tl_monotype_sized_hash64(hash, self->list.xs);
        if (tl_list == self->tag) hash = str_array_hash64(hash, self->list.fvs);
    } break;
    }

    return hash;
}

str tl_monotype_to_string(allocator *alloc, tl_monotype const *self) {

    str_build b = str_build_init(alloc, 64);

    switch (self->tag) {

    case tl_var: {
        char buf[64];
        snprintf(buf, sizeof buf, "t%u", self->var);
        str_build_cat(&b, str_init(alloc, buf));
    } break;

    case tl_weak: {
        char buf[64];
        snprintf(buf, sizeof buf, "w%u", self->var);
        str_build_cat(&b, str_init(alloc, buf));
    } break;

    case tl_cons_inst: {
        str_build_cat(&b, self->cons_inst->def->name);
        if (self->cons_inst->args.size) {
            str_build_cat(&b, S(" "));
            forall(i, self->cons_inst->args) {
                str_build_cat(&b, tl_monotype_to_string(alloc, self->cons_inst->args.v[i]));
                if (i + 1 < self->cons_inst->args.size) str_build_cat(&b, S(" -> "));
            }
        }
    } break;

    case tl_list: {
        if (self->list.fvs.size) {
            str_build_cat(&b, S("["));
            forall(i, self->list.fvs) {
                str_build_cat(&b, self->list.fvs.v[i]);
                if (i + 1 < self->list.fvs.size) str_build_cat(&b, S(" "));
            }
            str_build_cat(&b, S("] "));
        }
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string(alloc, self->list.xs.v[i]));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(" -> "));
        }
        str_build_cat(&b, S(")"));
    } break;

    case tl_tuple: {
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string(alloc, self->list.xs.v[i]));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S(")"));

    } break;
    }

    return str_build_finish(&b);
}

str tl_polytype_to_string(allocator *alloc, tl_polytype const *self) {
    str_build b = str_build_init(alloc, 64);

    if (self->quantifiers.size) {
        str_build_cat(&b, S("forall"));
        forall(i, self->quantifiers) {
            char buf[64];
            snprintf(buf, sizeof buf, "t%u", self->quantifiers.v[i]);
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
    *self              = (tl_type_subs){.alloc = alloc};
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

    // make x the new root, with rank >= y rank
    self->v[y].parent = x;
    if (self->v[x].rank == self->v[y].rank) self->v[x].rank++;
}

static int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right,
                      tl_monotype const *lhs, tl_monotype const *rhs, type_error_cb_fun cb, void *user);
static int tl_type_subs_unify_tv(tl_type_subs *, tl_type_variable, tl_type_variable, type_error_cb_fun,
                                 void *);
static int tl_type_subs_unify_tv_weak(tl_type_subs *, tl_type_variable, tl_monotype const *,
                                      type_error_cb_fun, void *);
static int tl_type_subs_unify_weak(tl_type_subs *, tl_monotype const *weak, tl_monotype const *,
                                   type_error_cb_fun, void *);

int        tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype const *left, tl_monotype const *right,
                                   type_error_cb_fun cb, void *user) {

    if (!left || !right) return 1;

    switch (left->tag) {

    case tl_var:
        switch (right->tag) {

        case tl_var:       return tl_type_subs_unify_tv(subs, left->var, right->var, cb, user);
        case tl_weak:      return tl_type_subs_unify_tv_weak(subs, left->var, right, cb, user);

        case tl_cons_inst:

        case tl_list:
        case tl_tuple:     return tl_type_subs_unify(subs, left->var, right, cb, user);
        }
        break;

    case tl_weak:
        switch (right->tag) {

        case tl_var: return tl_type_subs_unify_tv_weak(subs, left->var, right, cb, user);

        case tl_weak:
            // unify two weak variables: put them in same equivalence class
            return tl_type_subs_unify_tv(subs, left->var, right->var, cb, user);

        case tl_cons_inst:
        case tl_list:
        case tl_tuple:     return tl_type_subs_unify_weak(subs, left, right, cb, user);
        }
        break;

    case tl_cons_inst:
        switch (right->tag) {

        case tl_var:  return tl_type_subs_unify(subs, right->var, left, cb, user);
        case tl_weak: return tl_type_subs_unify_weak(subs, right, left, cb, user);

        case tl_cons_inst:
            if (left->cons_inst->def != right->cons_inst->def) {
                if (cb) cb(user, left, right);
                return 1;
            }
            return unify_list(subs, left->cons_inst->args, right->cons_inst->args, left, right, cb, user);

        case tl_list:
        case tl_tuple:

            if (cb) cb(user, left, right);
            return 1;
        }
        break;

    case tl_list:
        switch (right->tag) {

        case tl_var:  return tl_type_subs_unify(subs, right->var, left, cb, user);
        case tl_weak: return tl_type_subs_unify_weak(subs, right, left, cb, user);

        case tl_cons_inst:
        case tl_tuple:
            if (cb) cb(user, left, right);
            return 1;

        case tl_list: return unify_list(subs, left->list.xs, right->list.xs, left, right, cb, user);
        }

        break;

    case tl_tuple:
        switch (right->tag) {

        case tl_var:  return tl_type_subs_unify(subs, right->var, left, cb, user);
        case tl_weak: return tl_type_subs_unify_weak(subs, right, left, cb, user);

        case tl_cons_inst:
        case tl_list:
            if (cb) cb(user, left, right);
            return 1;

        case tl_tuple: return unify_list(subs, left->list.xs, right->list.xs, left, right, cb, user);
        }

        break;
    }
}

int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype const *lhs,
               tl_monotype const *rhs, type_error_cb_fun cb, void *user) {

    if (left.size != right.size) {
        if (cb) cb(user, lhs, rhs);
        return 1;
    }

    forall(i, left) {
        if (tl_type_subs_unify_mono(subs, left.v[i], right.v[i], cb, user)) {
            if (cb) cb(user, lhs, rhs);
            return 1;
        }
    }

    return 0;
}

int tl_type_subs_monotype_occurs(tl_type_subs *self, tl_type_variable tv, tl_monotype const *mono) {
    if (!mono) return 0;

    switch (mono->tag) {
    case tl_var:
    case tl_weak: {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) return 1;
        tl_monotype const *resolved = self->v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs(self, tv, resolved);

    } break;

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == mono->tag) arr = mono->cons_inst->args;
        else arr = mono->list.xs;
        forall(i, arr) {
            if (tl_type_subs_monotype_occurs(self, tv, arr.v[i])) return 1;
        }
    } break;
    }

    return 0;
}

static int tl_type_subs_unify_tv(tl_type_subs *self, tl_type_variable left, tl_type_variable right,
                                 type_error_cb_fun cb, void *user) {

    if (left == right) return 0;

    tl_type_variable left_root  = uf_find(self, left);
    tl_type_variable right_root = uf_find(self, right);
    if (left_root == right_root) return 0; // already in same equivalence class

    tl_monotype const *left_type  = self->v[left_root].type;
    tl_monotype const *right_type = self->v[right_root].type;
    if (left_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user)) {
            return 1;
        }
    }

    // union the two classes
    uf_union(self, left_root, right_root);

    // preserve the resolved type, if any
    tl_type_variable union_root = uf_find(self, left_root);
    self->v[union_root].type    = left_type ? left_type : right_type;
    return 0;
}

static int tl_type_subs_unify_tv_weak(tl_type_subs *self, tl_type_variable left, tl_monotype const *right,
                                      type_error_cb_fun cb, void *user) {

    if (tl_weak != right->tag) fatal("logic error");

    tl_type_variable   left_root  = uf_find(self, left);

    tl_monotype const *left_type  = self->v[left_root].type;
    tl_monotype const *right_type = right;
    if (left_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->v[left_root].type = tl_monotype_clone(self->alloc, right);

    return 0;
}

static int tl_type_subs_unify_weak(tl_type_subs *self, tl_monotype const *weak, tl_monotype const *right,
                                   type_error_cb_fun cb, void *user) {

    if (tl_weak != weak->tag) fatal("logic error");

    tl_type_variable   weak_root  = uf_find(self, weak->var);

    tl_monotype const *weak_type  = self->v[weak_root].type;
    tl_monotype const *right_type = right;
    if (weak_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, weak_type, right_type, cb, user)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->v[weak_root].type = tl_monotype_clone(self->alloc, right);

    return 0;
}

int tl_type_subs_unify(tl_type_subs *self, tl_type_variable tv, tl_monotype const *mono,
                       type_error_cb_fun cb, void *user) {

    if (tl_type_subs_monotype_occurs(self, tv, mono)) return 1;

    tl_type_variable tv_root = uf_find(self, tv);

    switch (mono->tag) {
    case tl_var:
        // case 1: both are tvs
        return tl_type_subs_unify_tv(self, tv, mono->var, cb, user);
    case tl_weak:
        // case 2: one is weak type variable
        return tl_type_subs_unify_tv_weak(self, tv, mono, cb, user);

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        // case 3: tv = concrete type or arrow or tuple
        tl_monotype const *tv_type = self->v[tv_root].type;
        if (tv_type) {
            // must unify
            return tl_type_subs_unify_mono(self, tv_type, mono, cb, user);
        }

        // store the type at the root
        self->v[tv_root].type = tl_monotype_clone(self->alloc, mono);

    } break;
    }

    return 0;
}

void tl_monotype_substitute(allocator *alloc, tl_monotype *self, tl_type_subs const *subs,
                            hashmap *exclude) {
    // exclude may be null
    if (!self) return;

    switch (self->tag) {

    case tl_var:
    case tl_weak: {

        if (exclude && hset_contains(exclude, &self->var, sizeof self->var)) return;
        tl_type_variable root = uf_find((tl_type_subs *)subs, self->var);
        if (exclude && hset_contains(exclude, &root, sizeof root)) return;

        tl_monotype const *resolved = subs->v[root].type;
        if (resolved) {
            *self = *resolved;
        } else {
            // update to representative tv
            self->var = root;
        }

    } break;

    case tl_cons_inst:
    case tl_list:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) {
            tl_monotype_substitute(alloc, (tl_monotype *)arr.v[i], subs, exclude); // const cast
        }

    } break;
    }
}

static void tl_polytype_substitute_ext(allocator *alloc, tl_polytype *self, tl_type_subs const *subs,
                                       hashmap **exclude) {
    if (exclude) map_reset(*exclude);

    if (exclude && self->quantifiers.size) {
        forall(i, self->quantifiers) {
            hset_insert(exclude, &self->quantifiers.v[i], sizeof(tl_type_variable));
        }
    }

    tl_monotype_substitute(alloc, (tl_monotype *)self->type, subs, exclude ? *exclude : null); // const cast
}

void tl_polytype_substitute(allocator *alloc, tl_polytype *self, tl_type_subs const *subs) {
    hashmap *exclude = null;

    if (self->quantifiers.size) exclude = map_create(alloc, sizeof(tl_type_variable), 8);
    tl_polytype_substitute_ext(alloc, self, subs, exclude ? &exclude : null);
    if (exclude) map_destroy(&exclude);
}

tl_polytype tl_polytype_wrap(tl_monotype const *mono) {
    return (tl_polytype){.type = mono};
}

void tl_type_subs_apply(tl_type_subs *subs, tl_type_env *env) {

    hashmap         *exclude = map_create(subs->alloc, sizeof(tl_type_variable), 8);

    hashmap_iterator iter    = {0};
    while (map_iter(env->map, &iter)) {
        tl_polytype *poly = *(tl_polytype **)iter.data;
        tl_polytype_substitute_ext(subs->alloc, poly, subs, &exclude);
    }

    map_destroy(&exclude);
}

// --------------------------------------------------------------------------

str tl_type_subs_to_string(allocator *alloc, tl_type_subs const *self) {
    return str_copy(alloc, S("not implemented"));
    (void)self;
}

// -- env --

void tl_type_env_log(tl_type_env *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->map, &iter)) {
        str                name     = str_init_n(self->transient, iter.key_ptr, iter.key_size);
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

        tl_monotype const *type = self->v[root].type;
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

//

u64 tl_monotype_sized_hash64(u64 seed, tl_monotype_sized arr) {
    u64 hash = seed;
    forall(i, arr) {
        u64 h = tl_monotype_hash64(arr.v[i]);
        hash  = hash64_combine(hash, &h, sizeof h);
    }
    return hash;
}

tl_monotype_sized tl_monotype_sized_clone(allocator *alloc, tl_monotype_sized in) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, in.size);
    forall(i, in) {
        tl_monotype *ty = tl_monotype_clone(alloc, in.v[i]);
        array_push(arr, ty);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

tl_polytype_sized tl_polytype_sized_clone(allocator *alloc, tl_polytype_sized polys) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_polytype const *clone = tl_polytype_clone(alloc, polys.v[i]);
        array_push(arr, clone);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype const *tl_monotype_sized_last(tl_monotype_sized arr) {
    if (!arr.size) return null;
    return arr.v[arr.size - 1];
}

tl_polytype_sized tl_monotype_sized_clone_poly(allocator *alloc, tl_monotype_sized monos) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, monos.size);
    forall(i, monos) {
        tl_polytype const *poly = tl_polytype_absorb_mono(alloc, tl_monotype_clone(alloc, monos.v[i]));
        array_push(arr, poly);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype_sized tl_polytype_sized_concrete(allocator *alloc, tl_polytype_sized polys) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_monotype const *mono = tl_polytype_concrete(polys.v[i]);
        array_push(arr, mono);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

static void log(tl_type_env const *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    char buf[256];

    snprintf(buf, sizeof buf, "tl_type_env: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
