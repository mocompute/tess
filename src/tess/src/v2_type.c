#include "v2_type.h"
#include "array.h"
#include "str.h"

#include <stdio.h>

// -- monotype --

tl_monotype tl_monotype_init_tv(tl_type_variable tv) {
    return (tl_monotype){.tag = tl_var, .var = tv};
}

tl_monotype tl_monotype_init_arrow(tl_type_arrow arrow) {
    return (tl_monotype){.tag = tl_arrow, .arrow = arrow};
}

tl_monotype tl_monotype_init_constructor_inst(tl_type_constructor_inst cons) {
    return (tl_monotype){.tag = tl_cons, .cons = cons};
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
static void tl_type_arrow_collect_free_variables(tl_type_variable_array *, tl_type_arrow const *);
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

static void tl_type_arrow_collect_free_variables(tl_type_variable_array *out, tl_type_arrow const *arrow) {
    tl_monotype_collect_free_variables(out, arrow->left);
    tl_monotype_collect_free_variables(out, arrow->right);
}

static void tl_monotype_collect_free_variables(tl_type_variable_array *out, tl_monotype const *mono) {
    switch (mono->tag) {
    case tl_cons:  return tl_type_constructor_inst_collect_free_variables(out, &mono->cons);
    case tl_var:   return tl_type_variable_collect_free_variables(out, &mono->var);
    case tl_arrow: return tl_type_arrow_collect_free_variables(out, &mono->arrow);
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

str tl_type_arrow_to_string(allocator *alloc, tl_type_arrow const *self) {
    str_build b = str_build_init(alloc, 64);
    {
        str left = tl_monotype_to_string(alloc, self->left);
        str_build_cat(&b, left);
        str_deinit(alloc, &left);
    }
    str_build_cat(&b, S(" -> "));
    {
        str right = tl_monotype_to_string(alloc, self->left);
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

tl_type_env *tl_type_env_create(allocator *alloc) {
    tl_type_env *self = new (alloc, tl_type_env);

    self->names       = (str_array){.alloc = alloc};
    self->types       = (tl_type_v2_array){.alloc = alloc};
    return self;
}

void tl_type_env_destroy(allocator *alloc, tl_type_env **p) {
    array_free((*p)->names);
    array_free((*p)->types);
    alloc_free(alloc, *p);
    *p = null;
}
