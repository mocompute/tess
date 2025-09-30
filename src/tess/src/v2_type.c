#include "v2_type.h"
#include "alloc.h"
#include "array.h"
#include "hashmap.h"
#include "str.h"

#include <stdio.h>

// -- monotype --

tl_monotype tl_monotype_init_tv(tl_type_variable tv) {
    return (tl_monotype){.tag = tl_var, .var = tv};
}

tl_monotype tl_monotype_init_arrow(tl_type_v2_arrow arrow) {
    return (tl_monotype){.tag = tl_arrow, .arrow = arrow};
}

tl_monotype tl_monotype_alloc_arrow(allocator *alloc, tl_monotype left, tl_monotype right) {
    tl_monotype *pleft  = tl_monotype_create(alloc, left);
    tl_monotype *pright = tl_monotype_create(alloc, right);
    tl_monotype  arrow  = tl_monotype_init_arrow((tl_type_v2_arrow){.left = pleft, .right = pright});
    return arrow;
}

void tl_monotype_dealloc(allocator *alloc, tl_monotype *self) {
    switch (self->tag) {
    case tl_cons:
        array_free(self->cons.args);
        str_deinit(alloc, &self->cons.name);
        break;
    case tl_arrow:
        alloc_free(alloc, self->arrow.left);
        alloc_free(alloc, self->arrow.right);
        break;

    case tl_var:
    case tl_nil: break;
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

// -- type --

tl_type_v2 tl_type_init_mono(tl_monotype mono) {
    return (tl_type_v2){.tag = tl_mono, .mono = mono};
}

tl_type_v2 tl_type_init_scheme(tl_type_scheme scheme) {
    return (tl_type_v2){.tag = tl_scheme, .scheme = scheme};
}

//

static void tl_monotype_collect_free_variables(tl_type_variable_array *, tl_monotype const *);
static void tl_type_variable_collect_free_variables(tl_type_variable_array *, tl_type_variable const *);
static void tl_type_arrow_collect_free_variables(tl_type_variable_array *, tl_type_v2_arrow const *);
static void tl_type_scheme_collect_free_variables(tl_type_variable_array *, tl_type_scheme const *);

void        tl_type_v2_collect_free_variables(tl_type_variable_array *, tl_type_v2 const *);
void        tl_type_env_free_variables(tl_type_env const *, tl_type_variable_array *);

static void tl_type_constructor_inst_collect_free_variables(tl_type_variable_array         *out,
                                                            tl_type_constructor_inst const *inst) {
    forall(i, inst->args) {
        tl_monotype_collect_free_variables(out, &inst->args.v[i]);
    }
}

static void tl_type_variable_collect_free_variables(tl_type_variable_array *out,
                                                    tl_type_variable const *var) {
    array_set_insert(*out, *var);
}

static void tl_type_arrow_collect_free_variables(tl_type_variable_array *out,
                                                 tl_type_v2_arrow const *arrow) {
    tl_monotype_collect_free_variables(out, arrow->left);
    tl_monotype_collect_free_variables(out, arrow->right);
}

static void tl_monotype_collect_free_variables(tl_type_variable_array *out, tl_monotype const *mono) {
    switch (mono->tag) {
    case tl_cons:  return tl_type_constructor_inst_collect_free_variables(out, &mono->cons);
    case tl_var:   return tl_type_variable_collect_free_variables(out, &mono->var);
    case tl_arrow: return tl_type_arrow_collect_free_variables(out, &mono->arrow);
    case tl_nil:   return;
    }
}

static void tl_type_scheme_collect_free_variables(tl_type_variable_array *out,
                                                  tl_type_scheme const   *scheme) {
    // fv(type) - quantifiers

    tl_type_variable_array type_vars = {.alloc = out->alloc};
    tl_monotype_collect_free_variables(&type_vars, &scheme->type);

    array_set_difference(*out, type_vars, scheme->quantifiers);
    array_free(type_vars);
}

void tl_type_v2_collect_free_variables(tl_type_variable_array *out, tl_type_v2 const *type) {

    switch (type->tag) {
    case tl_mono:   return tl_monotype_collect_free_variables(out, &type->mono);
    case tl_scheme: return tl_type_scheme_collect_free_variables(out, &type->scheme);
    }
}

void tl_type_env_free_variables(tl_type_env const *env, tl_type_variable_array *out) {
    forall(i, env->types) {
        tl_type_v2_collect_free_variables(out, &env->types.v[i]);
    }
}

//

static void tl_type_subs_reserve(tl_type_subs *self, u32 n) {
    array_reserve(self->froms, n);
    array_reserve(self->tos, n);
}

static void tl_monotype_substitute(tl_monotype *self, tl_type_variable var, tl_monotype mono);
static void tl_type_scheme_substitute(tl_type_scheme *self, tl_type_variable var, tl_monotype mono);

static void tl_monotype_substitute(tl_monotype *self, tl_type_variable var, tl_monotype mono) {
    switch (self->tag) {
    case tl_cons: {
        forall(i, self->cons.args) {
            tl_monotype_substitute(&self->cons.args.v[i], var, mono);
        }
    } break;

    case tl_var: {
        if (self->var == var) *self = mono;
    } break;

    case tl_arrow: {
        tl_monotype_substitute(self->arrow.left, var, mono);
        tl_monotype_substitute(self->arrow.right, var, mono);
    } break;

    case tl_nil: break;
    }
}

static void tl_monotype_apply_subs(tl_monotype *self, tl_type_subs const *subs) {
    forall(i, subs->froms) {
        tl_monotype_substitute(self, subs->froms.v[i], subs->tos.v[i]);
    }
}

static void tl_type_scheme_substitute(tl_type_scheme *self, tl_type_variable var, tl_monotype mono) {
    forall(i, self->quantifiers) {
        if (var == self->quantifiers.v[i]) return; // do not substitute quantifier
    }

    tl_monotype_substitute(&self->type, var, mono);
}

static void tl_type_v2_substitute(tl_type_v2 *self, tl_type_variable var, tl_monotype mono) {
    switch (self->tag) {
    case tl_mono:   return tl_monotype_substitute(&self->mono, var, mono);
    case tl_scheme: return tl_type_scheme_substitute(&self->scheme, var, mono);
    }
}

static void tl_type_v2_apply_subs(tl_type_v2 *self, tl_type_subs const *subs) {
    forall(i, subs->froms) {
        tl_type_v2_substitute(self, subs->froms.v[i], subs->tos.v[i]);
    }
}

//

tl_type_subs *tl_type_subs_create(allocator *alloc) {
    tl_type_subs *self = new (alloc, tl_type_subs);
    self->froms        = (tl_type_variable_array){.alloc = alloc};
    self->tos          = (tl_monotype_array){.alloc = alloc};
    return self;
}

void tl_type_subs_destroy(allocator *alloc, tl_type_subs **p) {
    array_free((*p)->froms);
    array_free((*p)->tos);
    alloc_free(alloc, *p);
    *p = null;
}

tl_type_subs *tl_type_subs_compose(allocator *alloc, tl_type_subs const *base, tl_type_subs const *subs) {

    tl_type_subs *out = tl_type_subs_create(alloc);
    tl_type_subs_reserve(out, base->froms.size);

    array_copy(out->froms, base->froms.v, base->froms.size);
    array_copy(out->tos, base->tos.v, base->tos.size);

    // apply subs to all monotypes in base
    forall(i, out->tos) {
        tl_monotype_apply_subs(&out->tos.v[i], subs);
    }

    // copy remaining substitutions where type var is not in base
    forall(i, subs->froms) {
        tl_type_variable from = subs->froms.v[i];

        // TODO optimize inner loop
        if (array_contains(out->froms, from)) continue;
        array_push(out->froms, from);
        array_push(out->tos, subs->tos.v[i]);
    }

    return out;
}

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
    str_build b = str_build_init(alloc, 64);
    {
        str left = tl_monotype_to_string(alloc, self->left);
        str_build_cat(&b, left);
        str_deinit(alloc, &left);
    }
    str_build_cat(&b, S(" -> "));
    {
        str right = tl_monotype_to_string(alloc, self->right);
        str_build_cat(&b, right);
        str_deinit(alloc, &right);
    }
    return str_build_finish(&b);
}

str tl_monotype_to_string(allocator *alloc, tl_monotype const *self) {
    switch (self->tag) {
    case tl_cons:  return tl_type_constructor_inst_to_string(alloc, &self->cons);
    case tl_var:   return tl_type_variable_to_string(alloc, &self->var);
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
        str tv = tl_type_variable_to_string(alloc, &self->quantifiers.v[i]);
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
    str_build b = str_build_init(alloc, 64);

    forall(i, self->froms) {
        str tv = tl_type_variable_to_string(alloc, &self->froms.v[i]);
        str_build_cat(&b, tv);
        str_deinit(alloc, &tv);
        str_build_cat(&b, S(" => "));
        str mono = tl_monotype_to_string(alloc, &self->tos.v[i]);
        str_build_cat(&b, mono);
        str_deinit(alloc, &mono);
    }

    return str_build_finish(&b);
}

// -- env --

static void add_type_cons(tl_type_env *self, tl_type_constructor_inst inst) {
    tl_type_env_add(self, inst.name, tl_type_init_mono(tl_monotype_init_constructor_inst(inst)));
}

static void make_builtin_type_constructors(tl_type_env *self) {

    tl_type_constructor_inst inst = {0};
    //
    inst.name = S("Int");
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

void tl_type_env_destroy(allocator *alloc, tl_type_env **p) {
    map_destroy(&(*p)->index);
    array_free((*p)->names);
    array_free((*p)->types);
    alloc_free(alloc, *p);
    *p = null;
}

u32 tl_type_env_add(tl_type_env *self, str name, tl_type_v2 type) {
    array_push(self->names, name);
    array_push(self->types, type);
    assert(self->names.size == self->types.size);
    return self->names.size - 1;
}

tl_type_v2 *tl_type_env_lookup(tl_type_env *self, str name) {
    u32 *found = str_map_get(self->index, name);
    return found ? &self->types.v[*found] : null;
}

// -- context --

tl_type_context tl_type_context_empty() {
    return (tl_type_context){
      .next_var   = 0,
      .next_quant = 0,
    };
}

tl_type_variable tl_type_context_new_variable(tl_type_context *self) {
    return (tl_type_variable)self->next_var++;
}

tl_type_quantifier tl_type_context_new_quantifier(tl_type_context *self) {
    return (tl_type_quantifier)self->next_quant++;
}
