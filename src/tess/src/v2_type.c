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
    self->definitions      = map_create(self->alloc, sizeof(tl_type_constructor_def *), 64); // key: str
    self->instances        = map_create(self->alloc, sizeof(tl_monotype *), 64); // key: registry_key

    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};

    tl_type_constructor_def_create(self, S("Nil"), empty_tv, empty, null);
    tl_type_constructor_def_create(self, S("Int"), empty_tv, empty, null);
    tl_type_constructor_def_create(self, S("Bool"), empty_tv, empty, null);
    tl_type_constructor_def_create(self, S("Float"), empty_tv, empty, null);
    tl_type_constructor_def_create(self, S("String"), empty_tv, empty, null);

    tl_type_variable_sized unary = {.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    unary.v[0]                   = tl_type_subs_fresh(subs);
    tl_type_constructor_def_create(self, S("Ptr"), unary, empty, null);

    tl_type_registry_instantiate(self, S("Nil"), null);
    tl_type_registry_instantiate(self, S("Int"), null);
    tl_type_registry_instantiate(self, S("Bool"), null);
    tl_type_registry_instantiate(self, S("Float"), null);
    tl_type_registry_instantiate(self, S("String"), null);

    return self;
}

tl_type_constructor_def const *tl_type_constructor_def_create(tl_type_registry *self, str name,
                                                              tl_type_variable_sized type_variables,
                                                              str_sized              field_names,
                                                              tl_monotype const     *field_types) {
    tl_type_constructor_def *def = alloc_malloc(self->alloc, sizeof *def);
    def->name                    = str_copy(self->alloc, name);
    def->type_variables          = type_variables;
    def->field_names             = field_names;
    def->field_types             = field_types;

    str_map_set_ptr(&self->definitions, def->name, def);
    return def;
}

typedef struct {
    u64 name_hash;
    u64 args_hash;

} registry_key;

tl_monotype const *tl_type_registry_instantiate(tl_type_registry *self, str name, tl_monotype const *args) {
    tl_monotype *type = null;
    registry_key key  = {.name_hash = str_hash64(name), .args_hash = tl_monotype_list_hash64(0, args)};
    if ((type = map_get_ptr(self->instances, &key, sizeof key))) return type;

    tl_type_constructor_def *def = str_map_get_ptr(self->definitions, name);
    if (!def) fatal("type cons name not found");
    if (tl_monotype_list_length(args) != def->type_variables.size) return null;

    tl_type_constructor_inst *inst = new (self->alloc, tl_type_constructor_inst);
    *inst = (tl_type_constructor_inst){.def = def, .args = tl_monotype_list_copy(self->alloc, args)};
    if (def->field_types) {
        tl_monotype const *def_types_copy = tl_monotype_list_copy(self->alloc, def->field_types);
        if (!inst->args) inst->args = def_types_copy;
        else tl_monotype_list_concat((tl_monotype *)inst->args, def_types_copy); // const cast
    }

    type  = new (self->alloc, tl_monotype);
    *type = (tl_monotype){.tag = tl_cons_inst, .cons_inst = inst};
    map_set_ptr(&self->instances, &key, sizeof key, type);

    return type;
}

tl_type_constructor_def const *tl_type_registry_get_def(tl_type_registry *self, str name) {
    return str_map_get_ptr(self->definitions, name);
}

tl_monotype const *tl_type_registry_nil(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Nil"), null);
}
tl_monotype const *tl_type_registry_int(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Int"), null);
}
tl_monotype const *tl_type_registry_float(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Float"), null);
}
tl_monotype const *tl_type_registry_bool(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Bool"), null);
}
tl_monotype const *tl_type_registry_string(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("String"), null);
}

tl_monotype const *tl_type_registry_ptr(tl_type_registry *self, tl_monotype const *arg) {
    return tl_type_registry_instantiate(self, S("Ptr"), arg);
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

tl_polytype const *tl_polytype_absorb_mono(allocator *alloc, tl_monotype const *mono) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->tag         = tl_poly_mono;
    self->quantifiers = (tl_type_variable_sized){0};
    self->type        = mono;
    return self;
}

tl_polytype const *tl_polytype_create_qv(allocator *alloc, tl_type_variable qv) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->tag         = tl_poly_mono;
    self->type        = tl_monotype_create_tv(alloc, qv);
    self->quantifiers =
      (tl_type_variable_sized){.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    self->quantifiers.v[0] = qv;
    return self;
}

tl_polytype const *tl_polytype_create_def(allocator *alloc, tl_type_constructor_def const *def) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->tag         = tl_poly_def;
    self->quantifiers = def->type_variables;
    self->def         = def;
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
    clone->tag         = orig->tag;
    clone->quantifiers = orig->quantifiers;

    switch (orig->tag) {
    case tl_poly_mono: clone->type = tl_monotype_clone(alloc, orig->type); break;

    case tl_poly_def:  {
        tl_type_constructor_def *def = alloc_malloc(alloc, sizeof *def);
        def->name                    = str_copy(alloc, orig->def->name);
        def->field_types             = tl_monotype_list_copy(alloc, orig->def->field_types);

        {
            tl_type_variable_array arr = {.alloc = alloc};
            array_push_many(arr, orig->def->type_variables.v, orig->def->type_variables.size);
            array_shrink(arr);
            def->type_variables = (tl_type_variable_sized)sized_all(arr);
        }
        {
            str_array arr = {.alloc = alloc};
            array_push_many(arr, orig->def->field_names.v, orig->def->field_names.size);
            array_shrink(arr);
            def->field_names                                  = (str_sized)sized_all(arr);
            forall(i, def->field_names) def->field_names.v[i] = str_copy(alloc, def->field_names.v[i]);
        }

        clone->def = def;

    } break;
    }
    return clone;
}

void tl_monotype_list_concat(tl_monotype *list, tl_monotype const *tail) {
    tl_monotype const *head = list;
    while (head->next) head = head->next;

    tl_monotype const *right = tail;
    switch (right->tag) {
    case tl_var:
    case tl_weak:
    case tl_cons_inst:
        ((tl_monotype *)head)->next = right; // const cast
        break;
    case tl_list:
    case tl_tuple:
        ((tl_monotype *)head)->next = right->list.head; // const cast
        break;
    }
}

void tl_polytype_list_append(allocator *alloc, tl_polytype *lhs, tl_polytype const *rhs) {

    if (rhs->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, lhs->quantifiers.size + rhs->quantifiers.size);
        array_push_many(arr, lhs->quantifiers.v, lhs->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, rhs->quantifiers) array_set_insert(arr, rhs->quantifiers.v[i]);
        lhs->quantifiers.size = arr.size;
        lhs->quantifiers.v    = arr.v;
        // leaks prior quantifiers array
    }

    tl_monotype const *list = lhs->type;
    assert(tl_list == list->tag);

    tl_monotype_list_concat((tl_monotype *)list->list.head, rhs->type); // const cast
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

        break;

    case tl_cons_inst: {
        tl_monotype *hd = (tl_monotype *)self->cons_inst->args; // const cast
        while (hd) {
            replace_tv(hd, map);
            hd = (tl_monotype *)hd->next; // const cast
        }
    } break;

    case tl_list:
    case tl_tuple: {
        tl_monotype *hd = (tl_monotype *)self->list.head; // const cast
        while (hd) {
            replace_tv(hd, map);
            hd = (tl_monotype *)hd->next; // const cast
        }
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

static void generalize(tl_monotype *self, tl_type_variable_array *quant) {
    if (!self) return;

    switch (self->tag) {
    case tl_var: array_set_insert(*quant, self->var); break;

    case tl_weak:
        // weak type variables are not generalizeable
        break;

    case tl_cons_inst: {
        tl_monotype *hd = (tl_monotype *)self->cons_inst->args; // const cast
        while (hd) {
            generalize(hd, quant);
            hd = (tl_monotype *)hd->next; // const cast
        }
    } break;

    case tl_list:
    case tl_tuple: {
        tl_monotype *hd = (tl_monotype *)self->list.head; // const cast
        while (hd) {
            generalize(hd, quant);
            hd = (tl_monotype *)hd->next; // const cast
        }

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

// -- monotype --

static u32 list_length(tl_monotype const *head, u32 count) {
    if (!head) return count;
    return list_length(head->next, count + 1);
}

u32 tl_monotype_list_length(tl_monotype const *head) {
    return list_length(head, 0);
}

tl_monotype const *tl_monotype_list_copy(allocator *alloc, tl_monotype const *head) {
    if (!head) return null;

    // copy list elements
    tl_monotype *copy          = null;

    copy                       = new (alloc, tl_monotype);
    *copy                      = *head;

    tl_monotype const *hd      = head->next;
    tl_monotype       *copy_hd = copy;
    while (hd) {
        copy_hd->next                 = new (alloc, tl_monotype);
        *(tl_monotype *)copy_hd->next = *hd; // const cast
        hd                            = hd->next;
        copy_hd                       = (tl_monotype *)copy_hd->next; // const cast
    }

    return copy;
}

tl_monotype const *tl_monotype_list_last(tl_monotype const *self) {
    if (tl_list != self->tag) return self;

    tl_monotype const *head = self->list.head;
    while (head->next) head = head->next;
    return head;
}

tl_monotype const *tl_monotype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_var, .var = tv};
    return self;
}

tl_monotype const *tl_monotype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak, .var = tv};
    return self;
}

nodiscard tl_monotype const *tl_monotype_create_fresh_weak(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak(self->alloc, tv);
}

tl_monotype const *tl_monotype_create_arrow(allocator *alloc, tl_monotype const *lhs,
                                            tl_monotype const *rhs) {
    tl_monotype *head = (tl_monotype *)tl_monotype_clone(alloc, lhs); // const cast
    head->next        = tl_monotype_clone(alloc, rhs);
    return tl_monotype_create_list(alloc, head);
}

tl_monotype const *tl_monotype_create_list(allocator *alloc, tl_monotype const *head) {
    assert(head);
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_list, .list = {.head = head}};
    return self;
}

tl_monotype const *tl_monotype_create_tuple(allocator *alloc, tl_monotype const *head) {
    assert(head);
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_tuple, .list = {.head = head}};
    return self;
}

tl_monotype const *tl_monotype_create_cons(allocator *alloc, tl_type_constructor_inst const *cons) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_cons_inst, .cons_inst = cons};
    return self;
}

tl_monotype const *tl_monotype_clone(allocator *alloc, tl_monotype const *orig) {

    if (!orig) fatal("logic error");
    tl_monotype *clone = alloc_malloc(alloc, sizeof *clone);

    switch (orig->tag) {
    case tl_var:  *clone = (tl_monotype){.tag = tl_var, .var = orig->var}; return clone;
    case tl_weak: *clone = (tl_monotype){.tag = tl_weak, .var = orig->var}; return clone;

    case tl_cons_inst:
        // copy the tl_type_constructor_inst struct
        *clone =
          (tl_monotype){.tag = tl_cons_inst, .cons_inst = alloc_malloc(alloc, sizeof *clone->cons_inst)};
        *(tl_type_constructor_inst *)clone->cons_inst = *orig->cons_inst; // const cast

        // clone the args list
        ((tl_type_constructor_inst *)clone->cons_inst)->args =
          tl_monotype_list_copy(alloc, orig->cons_inst->args); // const cast
        break;

    case tl_list:
    case tl_tuple:
        *clone =
          (tl_monotype){.tag = orig->tag, .list = {.head = tl_monotype_list_copy(alloc, orig->list.head)}};
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
        tl_monotype const *hd = self->list.head;
        while (hd) {
            if (!tl_monotype_is_concrete(hd)) return 0;
            hd = hd->next;
        }
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

void tl_monotype_sort_fvs(tl_monotype *self) {
    if (tl_list != self->tag) return;
    if (!self->list.fvs) return;
    if (!self->list.fvs->size) return;
    qsort(self->list.fvs->v, self->list.fvs->size, sizeof self->list.fvs->v[0], str_cmp_v);
}

str_sized tl_monotype_fvs(tl_monotype const *self) {
    if (tl_list != self->tag) return (str_sized){0};
    if (self->list.fvs) return *self->list.fvs;
    return (str_sized){0};
}

void tl_monotype_absorb_fvs(allocator *alloc, tl_monotype *self, str_sized fvs) {
    if (tl_list != self->tag) fatal("logic error");
    self->list.fvs  = new (alloc, str_sized);
    *self->list.fvs = fvs;
}

u64 tl_type_constructor_def_hash64(tl_type_constructor_def const *self) {
    u64 hash = str_hash64(self->name);
    hash     = hash64_combine(hash, &self->type_variables.size, sizeof self->type_variables.size);
    return hash;
}

u64 tl_monotype_list_hash64(u64 seed, tl_monotype const *head) {
    u64 hash = seed;
    while (head) {
        u64 h = tl_monotype_hash64(head);
        hash  = hash64_combine(hash, &h, sizeof h);
        head  = head->next;
    }
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
        hash         = tl_monotype_list_hash64(hash, self->cons_inst->args);
    } break;

    case tl_list:
    case tl_tuple: {
        hash = tl_monotype_list_hash64(hash, self->list.head);
        if (tl_list == self->tag && self->list.fvs) hash = str_array_hash64(hash, *self->list.fvs);
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
        tl_monotype const *arg = self->cons_inst->args;
        if (arg) {
            str_build_cat(&b, S(" "));
            while (arg) {
                str_build_cat(&b, tl_monotype_to_string(alloc, arg));
                if (arg->next) str_build_cat(&b, S(" -> "));
                arg = arg->next;
            }
        }
    } break;

    case tl_list: {
        if (self->list.fvs) {
            str_build_cat(&b, S("["));
            forall(i, *self->list.fvs) {
                str_build_cat(&b, self->list.fvs->v[i]);
                if (i + 1 < self->list.fvs->size) str_build_cat(&b, S(" "));
            }
            str_build_cat(&b, S("] "));
        }
        tl_monotype const *hd = self->list.head;
        str_build_cat(&b, S("("));
        while (hd) {
            str_build_cat(&b, tl_monotype_to_string(alloc, hd));
            if (hd->next) str_build_cat(&b, S(" -> "));
            hd = hd->next;
        }
        str_build_cat(&b, S(")"));
    } break;

    case tl_tuple: {
        tl_monotype const *hd = self->list.head;
        str_build_cat(&b, S("("));
        while (hd) {
            str_build_cat(&b, tl_monotype_to_string(alloc, hd));
            if (hd->next) str_build_cat(&b, S(", "));
            hd = hd->next;
        }
        str_build_cat(&b, S(")"));

    } break;
    }

    return str_build_finish(&b);
}

str tl_polytype_to_string(allocator *alloc, tl_polytype const *self) {
    str_build b = str_build_init(alloc, 64);

    switch (self->tag) {

    case tl_poly_mono:
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
        break;

    case tl_poly_def: {

        if (self->def->type_variables.size) {
            str_build_cat(&b, S("forall"));
            forall(i, self->def->type_variables) {
                char buf[64];
                snprintf(buf, sizeof buf, "t%u", self->def->type_variables.v[i]);
                str_build_cat(&b, S(" "));
                str_build_cat(&b, str_init(alloc, buf));
            }
            str_build_cat(&b, S(". "));
        }

        str_build_cat(&b, self->def->name);

        if (self->def->type_variables.size) {
            forall(i, self->def->type_variables) {
                char buf[64];
                snprintf(buf, sizeof buf, "t%u", self->def->type_variables.v[i]);
                str_build_cat(&b, S(" "));
                str_build_cat(&b, str_init(alloc, buf));
            }
        }

    } break;
    }
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

int unify_list(tl_type_subs *subs, tl_monotype const *left, tl_monotype const *right, type_error_cb_fun cb,
               void *user);

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
            return unify_list(subs, left->cons_inst->args, right->cons_inst->args, cb, user);

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

        case tl_list: return unify_list(subs, left->list.head, right->list.head, cb, user);
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

        case tl_tuple: return unify_list(subs, left->list.head, right->list.head, cb, user);
        }

        break;
    }
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
    if (!mono) return 0;

    switch (mono->tag) {
    case tl_var:
    case tl_weak: {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) return 1;
        tl_monotype const *resolved = self->v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs(self, tv, resolved);

    } break;

    case tl_cons_inst: {
        tl_monotype const *hd = mono->cons_inst->args;
        while (hd) {
            if (tl_type_subs_monotype_occurs(self, tv, hd)) return 1;
            hd = hd->next;
        }
    } break;

    case tl_list:
    case tl_tuple: {
        tl_monotype const *hd = mono->list.head;
        while (hd) {
            if (tl_type_subs_monotype_occurs(self, tv, hd)) return 1;
            hd = hd->next;
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

            // apply substitution, preserving list structure if any
            if (self->next) {
                resolved                        = tl_monotype_clone(alloc, resolved);
                ((tl_monotype *)resolved)->next = self->next; // const cast
            }

            *self = *resolved;
        } else {
            // update to representative tv
            self->var = root;
        }

    } break;

    case tl_cons_inst:
        tl_monotype_substitute(alloc, (tl_monotype *)self->cons_inst->args, subs, exclude); // const cast
        break;

    case tl_list:
    case tl_tuple: {
        tl_monotype const *hd = self->list.head;
        while (hd) {
            tl_monotype_substitute(alloc, (tl_monotype *)hd, subs, exclude); // const cast
            hd = hd->next;
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

static void log(tl_type_env const *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    char buf[256];

    snprintf(buf, sizeof buf, "tl_type_env: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
