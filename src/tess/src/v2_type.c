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
static void tl_type_constructor_inst_substitute(tl_type_constructor_inst *self, tl_type_variable var,
                                                tl_monotype mono);
static void tl_type_variable_substitute(tl_type_variable *self, tl_type_variable var, tl_monotype mono);
static void tl_type_arrow_substitute(tl_type_arrow *self, tl_type_variable var, tl_monotype mono);
static void tl_type_scheme_substitute(tl_type_scheme *self, tl_type_variable var, tl_monotype mono);

static void tl_type_v2_substitute(tl_type_v2 *self, tl_type_variable var, tl_monotype mono) {
    switch (self->tag) {
    case tl_mono:   return tl_monotype_substitute(&self->mono, var, mono);
    case tl_scheme: return tl_type_scheme_substitute(&self->scheme, var, mono);
    }
}

tl_type_subs *tl_type_subs_compose(allocator *alloc, tl_type_subs const *base, tl_type_subs const *subs) {

    tl_type_subs *out = tl_type_subs_create(alloc);
    tl_type_subs_reserve(out, base->froms.size);

    forall(i, base->froms) {
        tl_type_variable from = base->froms.v[i];
        tl_monotype      to   = base->tos.v[i];
    }

    return out;
}
