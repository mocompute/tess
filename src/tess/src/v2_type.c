#include "v2_type.h"
#include "array.h"

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
