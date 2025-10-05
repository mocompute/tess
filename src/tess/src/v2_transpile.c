#include "v2_transpile.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "str.h"
#include "v2_type.h"

#include <stdio.h>

#define TRANSPILE_ARENA_SIZE     32 * 1024
#define TRANSPILE_TRANSIENT_SIZE 32 * 1024
#define TRANSPILE_BUILD_SIZE     32 * 1024

struct transpile {
    allocator   *parent;
    allocator   *arena;
    allocator   *transient;

    tl_type_env *env;
    hashmap     *toplevels; // str => ast_node*

    str_build    build;

    u32          next_res;

    int          verbose;
};

extern char const *embed_std_c;

static str         next_res(transpile *);

static void        generate_decl(transpile *, str, tl_monotype const *);
static str         generate_expr(transpile *, tl_monotype const *, ast_node const *);
static void        generate_main(transpile *);
static void        generate_prototypes(transpile *);
static void        generate_toplevels(transpile *);
static void        generate_assign_lhs(transpile *, str);
static void        generate_assign(transpile *, str, str);

static void        cat(transpile *, str);
static void        cat_nl(transpile *);
static void        cat_sp(transpile *);
static void        cat_assign(transpile *);
static void        cat_double_slash(transpile *);
static void        cat_open_round(transpile *);
static void        cat_close_round(transpile *);
static void        cat_open_curly(transpile *);
static void        cat_open_curlyln(transpile *);
static void        cat_close_curly(transpile *);
static void        cat_semicolon(transpile *);
static void        cat_semicolonln(transpile *);
static void        cat_return(transpile *, str);
static void        catln(transpile *, str);
static void        cat_comment(transpile *, str);
static void        cat_commentln(transpile *, str);
static void        cat_i64(transpile *, i64);
static void        cat_f64(transpile *, f64);

tl_monotype       *env_lookup(transpile *, str);
static str         mangle_fun(transpile *, str); // allocates transient
static int         is_intrinsic(str);
static int         should_generate(str, tl_type_v2 const *);
static str         type_to_c(tl_type_v2 const *);
static str         type_to_c_mono(tl_monotype const *);
static str         arrow_rhs_to_c(tl_type_v2 const *);
static str         arrow_to_c_params(transpile *, tl_type_v2 const *, str_sized); // allocates transient

//

static void generate_prototypes(transpile *self) {
    forall(i, self->env->names) {
        str         name = self->env->names.v[i];
        tl_type_v2 *type = &self->env->types.v[i];

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(name, type)) continue;

        str ret = arrow_rhs_to_c(type);
        cat(self, ret);
        cat_sp(self);
        cat(self, mangle_fun(self, name));
        cat_open_round(self);
        cat(self, arrow_to_c_params(self, type, (str_sized){0}));
        cat_close_round(self);
        cat_semicolon(self);
        cat_nl(self);
    }
}

static void generate_toplevels(transpile *self) {
    forall(i, self->env->names) {
        str         name = self->env->names.v[i];
        tl_type_v2 *type = &self->env->types.v[i];

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(name, type)) continue;

        tl_monotype const *return_type = tl_type_v2_arrow_rightmost(&type->mono);
        ast_node          *node        = ast_node_str_map_get(self->toplevels, name);
        if (!node) fatal("function not found");

        ast_node      *body = null;
        ast_node_sized params;
        if (ast_let == node->tag) {
            params.size = node->let.n_parameters;
            params.v    = node->let.parameters;
            body        = node->let.body;
        } else if (ast_node_is_let_in_lambda(node)) {
            params.size = node->let_in.value->lambda_function.n_parameters;
            params.v    = node->let_in.value->lambda_function.parameters;
            body        = node->let_in.value->lambda_function.body;
        }
        if (!body) fatal("function body not found");

        str_array params_str = {.alloc = self->transient};
        array_reserve(params_str, params.size);
        forall(i, params) {
            array_push(params_str, params.v[i]->symbol.name);
        }

        str ret         = arrow_rhs_to_c(type);
        int res_is_void = str_eq(ret, S("void"));
        str res         = str_empty();
        if (!res_is_void) {
            res = next_res(self);
        }

        cat(self, ret);
        cat_sp(self);
        cat(self, mangle_fun(self, name));
        cat_open_round(self);
        cat(self, arrow_to_c_params(self, type, (str_sized)sized_all(params_str)));
        cat_close_round(self);
        cat_open_curlyln(self);

        str body_res = generate_expr(self, return_type, body);
        if (!res_is_void) {
            generate_decl(self, res, return_type);
            generate_assign(self, res, body_res);
            cat_return(self, res);
        }
        cat_close_curly(self);
        cat_nl(self);
    }
}

static void generate_main(transpile *self) {
    ast_node const *main = ast_node_str_map_get(self->toplevels, S("main"));
    if (!main) fatal("no main function");
    if (ast_let != main->tag) fatal("logic error");
    ast_node const *body = main->let.body;

    cat(self, S("int main(void) {"));
    cat_nl(self);

    str res = generate_expr(self, null, body);
    cat_return(self, res);

    cat_close_curly(self);
    cat_nl(self);
}

static void generate_assign_lhs(transpile *self, str var) {
    cat(self, var);
    cat_assign(self);
}

static void generate_assign(transpile *self, str lhs, str rhs) {
    cat(self, lhs);
    cat_assign(self);
    cat(self, rhs);
    cat_semicolonln(self);
}

static void generate_funcall_head(transpile *self, tl_monotype const *type, str name) {
    // TODO: function call context goes here

    (void)type;

    cat(self, mangle_fun(self, name));
    cat_open_round(self);
}

static str_array generate_args(transpile *self, ast_node_sized args, tl_monotype const *arrow) {
    // generate args to match arrow type
    str_array args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);
    arrow = tl_type_v2_arrow_head(arrow);
    forall(i, args) {
        if (!arrow) fatal("ran out of arrow");
        str res = generate_expr(self, arrow, args.v[i]);
        array_push(args_res, res);
        arrow = tl_type_v2_arrow_next(arrow);
    }
    return args_res;
}

static str generate_funcall(transpile *self, ast_node const *node) {
    assert(ast_node_is_named_application(node));
    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);

    // generate arguments: an array of variables will hold their values
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = generate_args(self, args, type);

    // declare variable to hold funcall result if it's not nil
    tl_monotype const *funcall_result_type = tl_type_v2_arrow_rightmost(type);
    str                res                 = str_empty(); // empty signals void result
    if (tl_nil != funcall_result_type->tag) {
        res = next_res(self);
        generate_decl(self, res, tl_type_v2_arrow_rightmost(type));
        generate_assign_lhs(self, res);
    }

    // function call
    generate_funcall_head(self, type, name);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static str generate_let_in(transpile *self, ast_node const *node) {
    assert(ast_let_in == node->tag);

    str                name        = ast_node_str(node->let_in.name);
    tl_monotype const *type        = env_lookup(self, name);
    tl_type_v2 const  *result_type = node->type_v2;
    assert(type);
    assert(tl_type_v2_is_mono(result_type));

    str value = generate_expr(self, type, node->let_in.value);

    generate_decl(self, name, type);
    generate_assign(self, name, value);

    str body = generate_expr(self, null, node->let_in.body);
    str res  = next_res(self);
    generate_decl(self, res, &result_type->mono);
    generate_assign(self, res, body);

    return res;
}

static str generate_str(transpile *self, str expr, tl_monotype const *type) {
    if (str_is_empty(expr)) return expr;
    str res = next_res(self);
    generate_decl(self, res, type);
    generate_assign_lhs(self, res);
    cat(self, expr);
    cat_semicolonln(self);
    return res;
}

static str generate_expr(transpile *self, tl_monotype const *type, ast_node const *node) {
    // This function is used to generate output to evaluate an expression with a given type, for example for
    // function arguments. We do it this way because type inference now works top down, meaning the type of
    // an object is held by the object's name, or point of application for unnamed literals.

    switch (node->tag) {
    case ast_named_function_application: return generate_funcall(self, node);
    case ast_let_in:                     return generate_let_in(self, node);
    case ast_i64:                        return generate_str(self, str_init_i64(self->transient, node->i64.val), type);
    case ast_f64:                        return generate_str(self, str_init_f64(self->transient, node->f64.val), type);
    case ast_symbol:                     return node->symbol.name;

    case ast_nil:
    case ast_any:
    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_bool:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_if_then_else:
    case ast_let_match_in:
    case ast_string:
    case ast_u64:
    case ast_user_type_definition:
    case ast_user_type_get:
    case ast_user_type_set:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function:
    case ast_lambda_function_application:
    case ast_let:
    case ast_tuple:
    case ast_user_type:
        cat_commentln(self, S("FIXME: generate_expr"));
        return str_copy(self->transient, S("FIXME_generate_expr"));
        break;
    }
}

static void generate_decl(transpile *self, str name, tl_monotype const *type) {
    if (tl_arrow == type->tag) fatal("not yet implemented");
    else if (tl_cons == type->tag) {
        str typec = type_to_c_mono(type);
        cat(self, typec);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    } else if (tl_nil == type->tag) fatal("can't declare a void type");
    else if (tl_var == type->tag || tl_quant == type->tag) fatal("got a type variable");
    else fatal("type not expected");
}

int transpile_compile(transpile *self, str_build *out_build) {

    self->build = str_build_init(self->parent, TRANSPILE_BUILD_SIZE);

    str_build_cat(&self->build, str_init_static(embed_std_c));

    generate_prototypes(self);
    cat_nl(self);
    generate_toplevels(self);
    cat_nl(self);
    generate_main(self);

    if (out_build) {
        *out_build = self->build;
    }
    return 0;
}

//

transpile *transpile_create(allocator *alloc, transpile_opts const *opts) {
    transpile *self = new (alloc, transpile);

    self->parent    = alloc;
    self->arena     = arena_create(alloc, TRANSPILE_ARENA_SIZE);
    self->transient = arena_create(alloc, TRANSPILE_TRANSIENT_SIZE);

    self->env       = opts->infer_result.env;
    self->toplevels = opts->infer_result.toplevels;

    self->next_res  = 0;

    self->verbose   = !!opts->verbose;

    return self;
}

void transpile_destroy(allocator *alloc, transpile **p) {
    if (!p || !*p) return;

    arena_destroy(alloc, &(*p)->transient);
    arena_destroy(alloc, &(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void transpile_set_verbose(transpile *self, int val) {
    self->verbose = val;
}

//

static str next_res(transpile *self) {
    char buf[64];
    int  len = snprintf(buf, sizeof buf, "tl_res%u", self->next_res++);
    return str_init_n(self->transient, buf, len);
}

//

static void cat(transpile *self, str s) {
    str_build_cat(&self->build, s);
}
static void cat_nl(transpile *self) {
    cat(self, S("\n"));
}
static void cat_sp(transpile *self) {
    cat(self, S(" "));
}
static void cat_assign(transpile *self) {
    cat(self, S(" = "));
}
static void cat_double_slash(transpile *self) {
    cat(self, S("// "));
}
static void cat_open_round(transpile *self) {
    cat(self, S("("));
}
static void cat_close_round(transpile *self) {
    cat(self, S(")"));
}
static void cat_open_curly(transpile *self) {
    cat(self, S("{"));
}
static void cat_open_curlyln(transpile *self) {
    cat(self, S("{\n"));
}
static void cat_close_curly(transpile *self) {
    cat(self, S("}"));
}
static void cat_semicolon(transpile *self) {
    cat(self, S(";"));
}
static void cat_semicolonln(transpile *self) {
    cat(self, S(";\n"));
}
static void cat_return(transpile *self, str s) {
    cat(self, S("return "));
    cat(self, s);
    cat(self, S(";\n"));
}
static void catln(transpile *self, str s) {
    cat(self, s);
    cat_nl(self);
}
static void cat_comment(transpile *self, str s) {
    cat(self, S("/* "));
    cat(self, s);
    cat(self, S(" */"));
}
static void cat_commentln(transpile *self, str s) {
    cat_comment(self, s);
    cat_nl(self);
}
static void cat_i64(transpile *self, i64 val) {
    str s = str_init_i64(self->transient, val);
    cat(self, s);
    str_deinit(self->transient, &s);
}
static void cat_f64(transpile *self, f64 val) {
    str s = str_init_f64(self->transient, val);
    cat(self, s);
    str_deinit(self->transient, &s);
}

//

static str mangle_fun(transpile *self, str s) {
    str_build b = str_build_init(self->transient, str_len(s) + 8);
    str_build_cat(&b, S("_tl_fun_"));
    str_build_cat(&b, s);

    return str_build_finish(&b);
}

static int is_intrinsic(str s) {
    return 0 == str_cmp_nc(s, "_tl_", 4);
}

static int should_generate(str name, tl_type_v2 const *type) {
    // return 0 if this function should not be generated by transpile
    // during its processing of functions in the environment.
    if (!tl_type_v2_is_arrow(type)) return 0;
    if (tl_type_v2_is_scheme(type)) return 0;
    if (str_eq(name, S("main"))) return 0;
    if (is_intrinsic(name)) return 0;
    return 1;
}

static str type_to_c(tl_type_v2 const *type) {
    if (tl_type_v2_is_scheme(type)) fatal("type scheme");
    tl_monotype const *mono = &type->mono;
    if (tl_cons == mono->tag) {
        if (str_eq(S("Int"), mono->cons.name)) {
            return S("int64_t");
        } else if (str_eq(S("Float"), mono->cons.name)) {
            return S("double");
        } else if (str_eq(S("String"), mono->cons.name)) {
            return S("char const*");
        } else fatal("unknown type constructor");
    } else if (tl_nil == mono->tag) {
        return S("void");
    } else fatal("not yet implemented");
}
static str type_to_c_mono(tl_monotype const *type) {
    tl_type_v2 t = tl_type_init_mono(*type);
    return type_to_c(&t);
}

static str arrow_rhs_to_c(tl_type_v2 const *type) {
    if (tl_type_v2_is_scheme(type)) fatal("type scheme");
    if (!tl_type_v2_is_arrow(type)) fatal("expected arrow");
    tl_monotype const *right = tl_type_v2_arrow_rightmost(&type->mono);
    return type_to_c_mono(right);
}

// tl_monotype const     *tl_type_v2_arrow_head(tl_monotype const *);
// tl_monotype const     *tl_type_v2_arrow_next(tl_monotype const *);

static str arrow_to_c_params(transpile *self, tl_type_v2 const *type, str_sized param_names) {
    if (tl_type_v2_is_scheme(type)) fatal("type scheme");
    if (!tl_type_v2_is_arrow(type)) fatal("expected arrow");

    str_build          b    = str_build_init(self->transient, 64);
    tl_monotype const *args = tl_type_v2_arrow_head(&type->mono);
    if (!tl_monotype_is_nil(args)) {
        int done = 0;
        for (u32 idx = 0; !done; ++idx) {
            str_build_cat(&b, type_to_c_mono(args));
            if (idx < param_names.size) {
                str_build_cat(&b, S(" "));
                str_build_cat(&b, param_names.v[idx]);
            }
            tl_monotype const *next = tl_type_v2_arrow_next(args);
            if (next) {
                str_build_cat(&b, S(", "));
                args = next;
            } else done = 1;
        }
    }

    return str_build_finish(&b);
}

tl_monotype *env_lookup(transpile *self, str name) {
    tl_type_v2 *type = tl_type_env_lookup(self->env, name);
    if (!type) fatal("type missing");
    if (tl_type_v2_is_scheme(type)) fatal("type scheme");
    return &type->mono;
}
