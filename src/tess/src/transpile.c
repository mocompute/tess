#include "transpile.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "infer.h"
#include "str.h"
#include "type.h"

#include <stdio.h>

#define TRANSPILE_ARENA_SIZE     32 * 1024
#define TRANSPILE_TRANSIENT_SIZE 32 * 1024
#define TRANSPILE_BUILD_SIZE     32 * 1024

struct transpile {
    allocator        *parent;
    allocator        *arena;
    allocator        *transient;

    tl_type_registry *registry;
    ast_node_sized    nodes;
    ast_node_sized    synthesized_nodes;
    tl_infer         *infer;
    tl_type_env      *env;
    tl_type_subs     *subs;
    hashmap          *toplevels;         // str => ast_node*
    hashmap          *structs;           // u64 set
    hashmap          *context_generated; // str set

    str_build         build;

    u32               next_res;

    int               verbose;
};

typedef struct {
    str_sized free_variables;

    // instead of emitting output to evaluate an expression and push it on the result stack, return a str
    // which can be used as an lvalue for the expression.
    int want_lvalue;
} eval_ctx;

extern char const *embed_std_c;

static str         next_res(transpile *);

static void        generate_decl(transpile *, str, tl_monotype const *);
static void        generate_decl_pointer(transpile *, str, tl_monotype const *);
static str         generate_expr(transpile *, tl_monotype const *, ast_node const *, eval_ctx *);
static str         generate_inline_lambda(transpile *, tl_monotype const *, ast_node const *, eval_ctx *);
static str         generate_let_in(transpile *, tl_monotype const *, ast_node const *, eval_ctx *);
static str         generate_if_then_else(transpile *, ast_node const *, eval_ctx *);
static void        generate_main(transpile *);
static str         generate_funcall(transpile *, ast_node const *, eval_ctx *);
static str         generate_funcall_intrinsic(transpile *, ast_node const *, eval_ctx *);
static void        generate_prototypes(transpile *, int);
static void        generate_structs(transpile *);
static void        generate_toplevel_values(transpile *);
static void        generate_toplevels(transpile *);
static void        generate_assign_lhs(transpile *, str);
static void        generate_assign(transpile *, str, str);
static void        generate_assign_field(transpile *, str, str, str);

static void        cat(transpile *, str);
static void        cat_nl(transpile *);
static void        cat_sp(transpile *);
static void        cat_ampersand(transpile *);
static void        cat_assign(transpile *);
static void        cat_commasp(transpile *);
static void        cat_dot(transpile *);
static void        cat_open_round(transpile *);
static void        cat_close_round(transpile *);
static void        cat_open_curly(transpile *);
static void        cat_open_curlyln(transpile *);
static void        cat_close_curly(transpile *);
static void        cat_close_square(transpile *);
static void        cat_semicolon(transpile *);
static void        cat_semicolonln(transpile *);
static void        cat_star(transpile *);
static void        cat_return(transpile *, str);
static void        catln(transpile *, str);
static void        cat_comment(transpile *, str);
static void        cat_commentln(transpile *, str);
// static void        cat_double_slash(transpile *);
static void cat_close_curlyln(transpile *);
// static void        cat_i64(transpile *, i64);
// static void        cat_f64(transpile *, f64);

tl_monotype const *env_lookup(transpile *, str); // may be null
static str         mangle_fun(transpile *, str); // allocates transient
static int         is_intrinsic(str);
static int         should_generate(str, tl_polytype const *);
static str         type_to_c(transpile *, tl_polytype const *);
static str         type_to_c_mono(transpile *, tl_monotype const *);
static str         arrow_rhs_to_c(transpile *, tl_polytype const *);
static str         arrow_to_c_params(transpile *, tl_polytype const *, str_sized); // allocates transient

//

static void generate_prototypes(transpile *self, int decl_static) {

    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {

        if (ast_node_is_utd(node)) continue;

        str                name = toplevel_name(node);
        tl_polytype const *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("missing type");

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(name, type)) continue;

        str ret = arrow_rhs_to_c(self, type);
        if (decl_static) cat(self, S("static "));
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

static str make_struct_name(allocator *alloc, tl_monotype const *type, u64 *out_hash) {
    u64  hash = tl_monotype_hash64(type);

    char buf[128];
    snprintf(buf, sizeof buf, "tl_struct_%" PRIu64, hash);
    if (out_hash) *out_hash = hash;
    return str_init(alloc, buf);
}

static str make_tuple_field_name(u32 i) {
    char buf[64];
    snprintf(buf, sizeof buf, "x%u", i);
    return str_init_small(buf);
}

static str generate_tuple(transpile *self, tl_monotype const *type, ast_node const *node, eval_ctx *ctx) {

    str res = next_res(self);
    generate_decl(self, res, type);

    forall(i, type->list.xs) {
        str field = make_tuple_field_name(i);

        // evaluate tuple element
        str value = generate_expr(self, type->list.xs.v[i], node->tuple.elements[i], ctx);

        // assign to field
        generate_assign_field(self, res, field, value);
    }
    return res;
}

static void generate_struct(transpile *self, tl_monotype const *type) {

    if (tl_monotype_is_tuple(type)) {
        u64 hash;
        str struct_name = make_struct_name(self->transient, type, &hash);
        if (hset_contains(self->structs, &hash, sizeof hash)) return;
        hset_insert(&self->structs, &hash, sizeof hash);

        cat(self, S("typedef struct "));
        cat(self, struct_name);
        catln(self, S(" {"));

        forall(i, type->list.xs) {
            str field = make_tuple_field_name(i);
            generate_decl(self, field, type->list.xs.v[i]);
        }

        cat(self, S("} "));
        cat(self, struct_name);
        cat_semicolonln(self);
    }
}

static void generate_structs(transpile *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype const *type = *(tl_polytype const **)iter.data;

        if (type->type->tag == tl_tuple) {
            if (tl_polytype_is_scheme(type)) fatal("type is scheme");
            if (!tl_polytype_is_concrete(type)) continue;
            generate_struct(self, type->type);
        }
    }
}

static void generate_user_types(transpile *self) {

    forall(i, self->synthesized_nodes) {
        ast_node *node = self->synthesized_nodes.v[i];

        if (!ast_node_is_utd(node)) continue;
        str                name = toplevel_name(node);
        tl_polytype const *poly = node->type;
        if (!tl_polytype_is_concrete(poly)) fatal("type scheme");
        if (!tl_monotype_is_inst(poly->type)) fatal("not a type constructor instance");

        tl_type_constructor_def const *def = poly->type->cons_inst->def;
        if (!def) fatal("missing type def");

        cat(self, S("typedef struct "));
        cat(self, name);
        catln(self, S(" {"));

        assert(def->field_names.size == poly->type->cons_inst->args.size);
        forall(i, def->field_names) {
            generate_decl(self, def->field_names.v[i], poly->type->cons_inst->args.v[i]);
        }

        cat(self, S("} "));
        cat(self, name);
        cat_semicolonln(self);
        cat_nl(self);
    }

    cat_nl(self);
}

static str context_name(transpile *self, str_sized fvs) {
    // generate struct name using hash of fvs
    u64  hash = str_array_hash64(0, fvs);
    char buf[64];
    snprintf(buf, sizeof buf, "tl_ctx_%" PRIu64, hash);
    return str_init(self->transient, buf);
}

static void generate_context_struct(transpile *self, str_sized fvs) {
    str name = context_name(self, fvs);
    if (str_hset_contains(self->context_generated, name)) return;
    str_hset_insert(&self->context_generated, name);

    // check types we don't want to emit because they are not concrete
    forall(i, fvs) {
        str                field      = fvs.v[i];
        tl_polytype const *field_type = tl_type_env_lookup(self->env, field);
        if (!field_type) return;
        if (!tl_polytype_is_concrete(field_type)) return;
    }

    cat(self, S("typedef struct "));
    cat(self, name);
    cat_sp(self);
    cat_open_curlyln(self);

    forall(i, fvs) {
        str                field      = fvs.v[i];
        tl_polytype const *field_type = tl_type_env_lookup(self->env, field);
        generate_decl_pointer(self, field, field_type->type);
        if (i + 1 < fvs.size) cat_nl(self);
    }

    cat_close_curly(self);
    cat_sp(self);
    cat(self, name);
    cat_semicolonln(self);
}

static str generate_ctx_var(transpile *self) {
    char buf[80];
    snprintf(buf, sizeof buf, "tl_ctx_var_%u", self->next_res++);
    str out = str_init(self->transient, buf);
    cat(self, out);
    return out;
}

static str generate_expr_symbol(transpile *self, tl_monotype const *type, str symbol_name, eval_ctx *ctx) {
    str name = symbol_name;
    if (tl_monotype_is_arrow(type)) name = mangle_fun(self, name);

    if (str_array_contains_one(ctx->free_variables, symbol_name)) // unmangled name
    {
        // generate reference through context
        return str_cat(self->transient, S("*tl_ctx->"), name);
    } else {
        return name;
    }
}

static str generate_context(transpile *self, str_sized fvs, eval_ctx *ctx) {
    if (!fvs.size) return str_empty();
    str name = context_name(self, fvs);

    cat(self, name);
    cat_sp(self);
    str ctx_var = generate_ctx_var(self);
    cat_assign(self);
    cat_open_curly(self);

    forall(i, fvs) {
        cat_dot(self);
        cat(self, fvs.v[i]);
        cat_assign(self);
        cat_ampersand(self);
        cat_open_round(self);

        str                name = fvs.v[i];
        tl_polytype const *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");
        name = generate_expr_symbol(self, type->type, name, ctx);
        cat(self, name);

        cat_close_round(self);

        if (i + 1 < fvs.size) cat_commasp(self);
    }

    cat_close_curly(self);
    cat_semicolonln(self);

    return ctx_var;
}

static void generate_toplevel_contexts(transpile *self) {

    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype const *type = *(tl_polytype const **)iter.data;

        if (type->type->tag == tl_list && type->type->list.fvs.size) {
            generate_context_struct(self, type->type->list.fvs);
        }
    }
    cat_nl(self);
}

static void generate_toplevel_values(transpile *self) {

    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        if (ast_node_is_let_in_lambda(node)) continue; // handled elsewhere
        if (!ast_node_is_let_in(node)) continue;
        str                name = ast_node_str(node->let_in.name);
        tl_polytype const *type = node->let_in.value->type;
        if (!tl_polytype_is_concrete(type)) continue;

        generate_decl(self, name, type->type);
    }
    cat_nl(self);

    // tl_init function

    cat(self, S("static void tl_init(void) {\n"));
    iter = (hashmap_iterator){0};
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        if (ast_node_is_let_in_lambda(node)) continue; // handled elsewhere
        if (!ast_node_is_let_in(node)) continue;
        str                name = ast_node_str(node->let_in.name);
        tl_polytype const *type = node->let_in.value->type;
        if (!tl_polytype_is_concrete(type)) continue;

        str value = generate_expr(self, type->type, node->let_in.value, null);
        generate_assign_lhs(self, name);
        cat(self, value);
        cat_semicolonln(self);
    }

    cat_close_curly(self);
    cat_nl(self);
}

static void generate_toplevels(transpile *self) {
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        if (ast_node_is_utd(node)) continue;
        str                name = toplevel_name(node);
        tl_polytype const *poly = tl_type_env_lookup(self->env, name);
        if (!poly) fatal("missing type");

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(name, poly)) continue;

        assert(poly->type->list.xs.size == 2);
        tl_monotype const *return_type = tl_monotype_sized_last(poly->type->list.xs);
        ast_node          *node        = ast_node_str_map_get(self->toplevels, name);
        if (!node) continue; // e.g. std.tl funs that aren't used

        ast_node *body = ast_node_body(node);
        if (!body) fatal("function body not found");

        ast_arguments_iter iter       = ast_node_arguments_iter(node);
        str_array          params_str = {.alloc = self->transient};
        array_reserve(params_str, iter.nodes.size);

        ast_node *param;
        while ((param = ast_arguments_next(&iter))) {
            array_push(params_str, param->symbol.name);
        }

        str ret         = arrow_rhs_to_c(self, poly);
        int res_is_void = str_eq(ret, S("void"));
        str res         = str_empty();
        if (!res_is_void) {
            res = next_res(self);
        }

        cat(self, ret); // return type
        cat_sp(self);
        cat(self, mangle_fun(self, name)); // fun name

        cat_open_round(self); // args
        cat(self, arrow_to_c_params(self, poly, (str_sized)sized_all(params_str)));
        cat_close_round(self);

        cat_open_curlyln(self); // body

        assert(tl_monotype_is_list(poly->type));
        eval_ctx ctx      = {.free_variables = poly->type->list.fvs};
        str      body_res = generate_expr(self, return_type, body, &ctx);
        if (!res_is_void) {
            generate_decl(self, res, return_type);
            generate_assign(self, res, body_res);
            cat_return(self, res);
        }
        cat_close_curly(self);
        cat_nl(self);
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
    cat(self, S("tl_init();\n"));

    eval_ctx ctx = {0};
    str      res = generate_expr(self, null, body, &ctx);
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

static void generate_assign_field(transpile *self, str lhs, str field, str rhs) {
    cat(self, lhs);
    cat_dot(self);
    cat(self, field);
    cat_assign(self);
    cat(self, rhs);
    cat_semicolonln(self);
}

static void generate_funcall_head_ext(transpile *self, str name, str ctx_var, u32 n_args, int do_mangle) {

    if (do_mangle) cat(self, mangle_fun(self, name));
    else cat(self, name);
    cat_open_round(self);

    if (!str_is_empty(ctx_var)) {
        cat_ampersand(self);
        cat(self, ctx_var);
        if (n_args) cat_commasp(self);
    }
}

static void generate_funcall_head(transpile *self, str name, str ctx_var, u32 n_args) {
    return generate_funcall_head_ext(self, name, ctx_var, n_args, 1);
}

static void generate_funcall_head_no_mangle(transpile *self, str name, str ctx_var, u32 n_args) {
    return generate_funcall_head_ext(self, name, ctx_var, n_args, 0);
}

static str_array generate_args(transpile *self, ast_node_sized args, tl_monotype const *arrow,
                               eval_ctx *ctx) {
    // generate args to match arrow type or type constructor
    str_array args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);

    tl_monotype_sized arr;
    if (tl_list == arrow->tag) {
        assert(2 == arrow->list.xs.size);
        assert(tl_tuple == arrow->list.xs.v[0]->tag);
        arr = arrow->list.xs.v[0]->list.xs;
    } else if (tl_cons_inst == arrow->tag) {
        arr = arrow->cons_inst->args;
    } else fatal("runtime error");

    assert(arr.size >= args.size);
    forall(i, args) {
        str res = generate_expr(self, arr.v[i], args.v[i], ctx);
        array_push(args_res, res);
    }
    return args_res;
}

static str generate_funcall_result(transpile *self, tl_monotype const *type, int do_assign_lhs) {
    assert(tl_monotype_is_list(type));
    tl_monotype const *funcall_result_type = tl_monotype_sized_last(type->list.xs);
    str                res                 = str_empty(); // empty signals void result

    if (!tl_monotype_is_nil(funcall_result_type)) {
        res = next_res(self);
        generate_decl(self, res, funcall_result_type);
        if (do_assign_lhs) generate_assign_lhs(self, res);
    }
    return res;
}

static str generate_type_constructor_named(transpile *self, ast_node const *node, eval_ctx *ctx) {
    // with named arguments (ast_assignment)

    str                name = ast_node_str(node->named_application.name);
    tl_monotype const *type = env_lookup(self, name);
    assert(tl_monotype_is_inst(type));

    str                            res = next_res(self);
    tl_type_constructor_def const *def = type->cons_inst->def;

    assert(def->field_names.size == node->named_application.n_arguments);
    assert(def->field_names.size == type->cons_inst->args.size);

    generate_decl(self, res, type);

    forall(i, def->field_names) {
        ast_node const *arg = node->named_application.arguments[i];
        if (!ast_node_is_assignment(arg)) fatal("expected named assignment node");

        // FIXME: no syntax check to see if the field name in the assignment is valid

        str arg_value = generate_expr(self, type->cons_inst->args.v[i], arg->assignment.value, ctx);

        cat(self, res);
        cat_dot(self);
        cat(self, ast_node_str(arg->assignment.name));
        cat_assign(self);
        cat(self, arg_value);
        cat_semicolonln(self);
    }

    return res;
}

static str generate_type_constructor(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_node_is_named_application(node));

    // divert if named arguments
    if (node->named_application.n_arguments && ast_node_is_assignment(node->named_application.arguments[0]))
        return generate_type_constructor_named(self, node, ctx);

    str                name = ast_node_str(node->named_application.name);
    tl_monotype const *type = env_lookup(self, name);
    assert(tl_monotype_is_inst(type));

    str                            res = next_res(self);
    tl_type_constructor_def const *def = type->cons_inst->def;

    assert(def->field_names.size == node->named_application.n_arguments);
    assert(def->field_names.size == type->cons_inst->args.size);

    generate_decl(self, res, type);

    forall(i, def->field_names) {
        str arg_value =
          generate_expr(self, type->cons_inst->args.v[i], node->named_application.arguments[i], ctx);

        cat(self, res);
        cat_dot(self);
        cat(self, def->field_names.v[i]);
        cat_assign(self);
        cat(self, arg_value);
        cat_semicolonln(self);
    }

    return res;
}

static str generate_funcall_std(transpile *self, ast_node const *node, eval_ctx *ctx) {

    // a funcall to a std function is fundamentally different: we don't have type information on the
    // function or the arguments.

    // generate untyped arguments
    str            name     = ast_node_str(node->named_application.name);
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);

    forall(i, args) {
        str res = generate_expr(self, null, args.v[i], ctx);
        array_push(args_res, res);
    }

    // Note: all std_ functions have nil result

    // function call
    generate_funcall_head(self, name, str_empty(), args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return str_empty();
}

static str generate_funcall_c(transpile *self, ast_node const *node, eval_ctx *ctx) {

    // a funcall to a c function is fundamentally different: we don't have type information on the
    // function or the arguments.

    // generate untyped arguments
    str name = ast_node_name_original(node->named_application.name);
    // str  name = ast_node_str(node->named_application.name);
    span s = str_span(&name);
    s.buf += 2;
    s.len -= 2;
    name                    = str_copy_span(self->arena, s);

    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);

    forall(i, args) {
        str res = generate_expr(self, null, args.v[i], ctx);
        array_push(args_res, res);
    }

    // declare variable to hold funcall result if it's not nil
    tl_monotype const *type = env_lookup(self, ast_node_str(node->named_application.name));
    str                res  = generate_funcall_result(self, type, 0);

    // function call
    if (!str_is_empty(res)) generate_assign_lhs(self, res);
    generate_funcall_head_no_mangle(self, name, str_empty(), args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static str generate_funcall(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_node_is_named_application(node));
    str name = ast_node_str(node->named_application.name);
    if (is_intrinsic(name)) return generate_funcall_intrinsic(self, node, ctx);
    if (0 == str_cmp_nc(name, "std_", 4)) return generate_funcall_std(self, node, ctx);
    if (0 == str_cmp_nc(name, "c_", 2)) return generate_funcall_c(self, node, ctx);

    tl_monotype const *type = env_lookup(self, name);
    if (!type) fatal("funcall with null type");

    // type constructor?
    if (tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

    // generate arguments: an array of variables will hold their values
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = generate_args(self, args, type, ctx);

    // declare variable to hold funcall result if it's not nil
    str res = generate_funcall_result(self, type, 0);

    assert(tl_monotype_is_list(type));
    str ctx_var = str_empty();
    if (type->list.fvs.size) {
        ctx_var = generate_context(self, type->list.fvs, ctx);
    }

    // function call
    if (!str_is_empty(res)) generate_assign_lhs(self, res);
    generate_funcall_head(self, name, ctx_var, args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static str generate_let_in_lambda(transpile *self, tl_monotype const *result_type, ast_node const *node,
                                  eval_ctx *ctx) {

    // don't declare or assign to name, because it is hoisted to a toplevel.

    str body = generate_expr(self, null, node->let_in.body, ctx);
    str res  = next_res(self);
    generate_decl(self, res, result_type);
    generate_assign(self, res, body);

    return res;
}

static str generate_let_in(transpile *self, tl_monotype const *result_type, ast_node const *node,
                           eval_ctx *ctx) {
    if (ast_node_is_let_in_lambda(node)) return generate_let_in_lambda(self, result_type, node, ctx);
    assert(ast_node_is_let_in(node));

    str                name = ast_node_str(node->let_in.name);
    tl_monotype const *type = env_lookup(self, name); // may be null

    if (type) {
        str value = generate_expr(self, type, node->let_in.value, ctx);

        if (!tl_monotype_is_nil(type)) {
            generate_decl(self, name, type);
            generate_assign(self, name, value);
        }
    }

    str body = generate_expr(self, null, node->let_in.body, ctx);
    if (!tl_monotype_is_nil(result_type)) {
        str res = next_res(self);
        generate_decl(self, res, result_type);
        generate_assign(self, res, body);
        return res;
    } else {
        return body;
    }
}

static str generate_if_then_else(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_if_then_else == node->tag);
    ast_node const    *cond        = node->if_then_else.condition;
    ast_node const    *yes         = node->if_then_else.yes;
    ast_node const    *no          = node->if_then_else.no;
    tl_monotype const *result_type = yes->type->type;

    str                cond_str    = generate_expr(self, null, cond, ctx);
    str                res         = next_res(self);

    generate_decl(self, res, result_type);
    cat(self, S("if ("));
    cat(self, cond_str);
    cat(self, S(") {\n"));

    str yes_str = generate_expr(self, null, yes, ctx);
    generate_assign(self, res, yes_str);
    cat(self, S("}\n"));

    if (!ast_node_is_nil(no)) {
        cat(self, S("else {\n"));
        str no_str = generate_expr(self, null, no, ctx);
        generate_assign(self, res, no_str);
        cat(self, S("}\n"));
    }

    return res;
}

static str generate_inline_lambda(transpile *self, tl_monotype const *result_type, ast_node const *node,
                                  eval_ctx *ctx) {
    assert(ast_node_is_lambda_application(node));

    ast_node_sized params = ast_node_sized_from_ast_array(node->lambda_application.lambda);
    ast_node_sized args   = ast_node_sized_from_ast_array((ast_node *)node);
    assert(params.size == args.size);

    if (node->lambda_application.lambda->type->quantifiers.size) fatal("type scheme");
    tl_monotype const *arrow    = node->lambda_application.lambda->type->type;

    str_array          args_res = generate_args(self, args, arrow, ctx);
    assert(args_res.size == params.size);

    // initialise parameters
    forall(i, params) {
        ast_node const *param = params.v[i];
        // if (ast_node_is_nil(param)) break;
        assert(ast_node_is_symbol(param));
        assert(!param->type->quantifiers.size);

        generate_decl(self, param->symbol.name, param->type->type);
        generate_assign_lhs(self, param->symbol.name);
        cat(self, args_res.v[i]);
        cat_semicolonln(self);
    }

    // declare variable to hold funcall result if it's not nil
    str res = generate_funcall_result(self, arrow, 0);

    // generate lambda body
    str lambda_res =
      generate_expr(self, result_type, node->lambda_application.lambda->lambda_function.body, ctx);
    generate_assign_lhs(self, res);
    cat(self, lambda_res);
    cat_semicolonln(self);
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

static str generate_type_get(transpile *self, tl_monotype const *type, ast_node const *node,
                             eval_ctx *ctx) {
    (void)ctx;
    assert(ast_user_type_get == node->tag);

    str res = next_res(self);
    generate_decl(self, res, type);
    generate_assign_lhs(self, res);
    cat(self, ast_node_str(node->user_type_get.struct_name));
    cat_dot(self);
    cat(self, ast_node_str(node->user_type_get.field_name));
    cat_semicolonln(self);
    return res;
}

static str generate_type_set(transpile *self, tl_monotype const *type, ast_node const *node,
                             eval_ctx *ctx) {
    assert(ast_user_type_set == node->tag);

    str value = generate_expr(self, type, node->user_type_set.value, ctx);

    generate_assign_field(self, ast_node_str(node->user_type_set.struct_name),
                          ast_node_str(node->user_type_get.field_name), value);

    str res = next_res(self);
    generate_decl(self, res, type);
    generate_assign(self, res, value);
    return res;
}

static str generate_body(transpile *self, tl_monotype const *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;

    str out = str_empty();
    forall(i, node->body.expressions) {
        out = generate_expr(self, null, node->body.expressions.v[i], ctx);
    }
    return out;
}

static str generate_binary_op(transpile *self, tl_monotype const *type, ast_node const *node,
                              eval_ctx *ctx) {
    assert(ast_binary_op == node->tag);
    str left  = generate_expr(self, type, node->binary_op.left, ctx);
    str right = generate_expr(self, type, node->binary_op.right, ctx);
    str op    = ast_node_str(node->binary_op.op);

    if (!ctx->want_lvalue) {
        str res = next_res(self);
        generate_decl(self, res, type);
        generate_assign_lhs(self, res);
        cat(self, left);
        cat(self, op);
        cat(self, right);

        // Note: special case: if op is [ close square bracket
        if (0 == str_cmp_c(op, "[")) cat_close_square(self);
        cat_semicolonln(self);
        return res;
    }

    else {
        str_build b = str_build_init(self->transient, 64);
        str_build_cat(&b, left);
        str_build_cat(&b, op);
        str_build_cat(&b, right);

        // Note: special case: if op is [ close square bracket
        if (0 == str_cmp_c(op, "[")) str_build_cat(&b, S("]"));
        return str_build_finish(&b);
    }
}

static str generate_unary_op(transpile *self, tl_monotype const *type, ast_node const *node,
                             eval_ctx *ctx) {
    assert(ast_unary_op == node->tag);
    str operand = generate_expr(self, type, node->unary_op.operand, ctx);
    str op      = ast_node_str(node->unary_op.op);

    if (!ctx->want_lvalue) {
        str res = next_res(self);
        generate_decl(self, res, type);
        generate_assign_lhs(self, res);
        cat(self, op);
        cat(self, operand);
        cat_semicolonln(self);
        return res;
    } else {
        str_build b = str_build_init(self->transient, 64);
        str_build_cat(&b, op);
        str_build_cat(&b, operand);
        return str_build_finish(&b);
    }
}

static str generate_reassignment(transpile *self, tl_monotype const *type, ast_node const *node,
                                 eval_ctx *ctx) {

    int save         = ctx->want_lvalue;
    ctx->want_lvalue = 1;

    str value        = generate_expr(self, type, node->assignment.value, ctx);
    str lhs          = generate_expr(self, null, node->assignment.name, ctx);
    generate_assign(self, lhs, value);

    ctx->want_lvalue = save;
    return value;
}

static str generate_return(transpile *self, tl_monotype const *type, ast_node const *node, eval_ctx *ctx) {
    // Note: handles return [expr] and break [expr]

    int has_value = !!node->return_.value;
    int is_break  = node->return_.is_break_statement;

    str value     = str_empty();
    if (has_value) value = generate_expr(self, type, node->return_.value, ctx);

    if (is_break) cat(self, S("break"));
    else cat(self, S("return"));

    if (has_value && !is_break) {
        cat_sp(self);
        cat(self, value);
    }

    cat_semicolonln(self);
    return value;
}

static str generate_while(transpile *self, tl_monotype const *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;

    // due to the stack-based transpiler, we rewrite while statement as follows:
    // while(1) { if (!condition) break; body }

    cat(self, S("while(1) "));
    cat_open_curlyln(self);

    str condition = generate_expr(self, null, node->while_.condition, ctx);
    cat(self, S("if"));
    cat_open_round(self);
    cat(self, S("!"));
    cat_open_round(self);
    cat(self, condition);
    cat_close_round(self);
    cat_close_round(self);
    cat(self, S("break;\n"));

    (void)generate_expr(self, null, node->while_.body, ctx);

    cat_close_curlyln(self);

    return str_empty();
}

static str generate_expr(transpile *self, tl_monotype const *type, ast_node const *node, eval_ctx *ctx) {
    // This function is used to generate output to evaluate an expression with a given type, for example for
    // function arguments. If type is null, then the type is taken from the expression. The str returned is
    // the name of the variable which holds the evaluated value.

    if (!type) {
        assert(node->type);
        type = node->type->type;
    }

    switch (node->tag) {
    case ast_named_function_application:  return generate_funcall(self, node, ctx);
    case ast_lambda_function_application: return generate_inline_lambda(self, type, node, ctx);
    case ast_let_in:                      return generate_let_in(self, type, node, ctx);
    case ast_i64:                         return generate_str(self, str_init_i64(self->transient, node->i64.val), type);
    case ast_u64:                         return generate_str(self, str_init_u64(self->transient, node->u64.val), type);
    case ast_f64:                         return generate_str(self, str_init_f64(self->transient, node->f64.val), type);
    case ast_bool:                        return generate_str(self, node->bool_.val ? S("1 /*true*/") : S("0 /*false*/"), type);
    case ast_string:
        return generate_str(self, str_cat_3(self->transient, S("\""), node->symbol.name, S("\"")), type);

    case ast_symbol: {

        // FIXME: may not need to use env for this
        str                name     = ast_node_str(node);
        tl_monotype const *env_type = env_lookup(self, name); // may be null
        if (env_type) type = env_type;

        return generate_expr_symbol(self, type, ast_node_str(node), ctx);
    }

    case ast_if_then_else: return generate_if_then_else(self, node, ctx);

    case ast_return:       return generate_return(self, type, node, ctx);
    case ast_while:        return generate_while(self, type, node, ctx);

    case ast_nil:          fatal("cannot generate nil");
    case ast_any:          fatal("cannot generate any");

    case ast_continue:     cat(self, S("continue;\n")); return str_empty();

    case ast_address_of:   {
        if (!tl_monotype_is_ptr(type)) fatal("runtime error");
        assert(1 == type->cons_inst->args.size);
        tl_monotype const *target_ty = type->cons_inst->args.v[0];
        return str_cat(self->transient, S("&"),
                       generate_expr(self, target_ty, node->address_of.target, ctx));
    } break;
    case ast_pointer_to: {
        if (!tl_monotype_is_ptr(type)) fatal("runtime error");
        assert(1 == type->cons_inst->args.size);
        tl_monotype const *target_ty = type->cons_inst->args.v[0];
        return str_cat(self->transient, generate_expr(self, target_ty, node->address_of.target, ctx),
                       S("*"));
    } break;

    case ast_dereference: {
        tl_monotype const *deref_ty = type;
        return str_cat(self->transient, S("*"),
                       generate_expr(self, deref_ty, node->dereference.target, ctx));
    } break;

    case ast_tuple:         return generate_tuple(self, type, node, ctx);

    case ast_user_type_get: return generate_type_get(self, type, node, ctx);
    case ast_user_type_set: return generate_type_set(self, type, node, ctx);

    case ast_body:          return generate_body(self, type, node, ctx);

    case ast_binary_op:     return generate_binary_op(self, type, node, ctx);
    case ast_unary_op:      return generate_unary_op(self, type, node, ctx);

    case ast_assignment:    return generate_reassignment(self, type, node, ctx);

    case ast_arrow:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_let_match_in:
    case ast_user_type_definition:
    case ast_begin_end:
    case ast_function_declaration:
    case ast_labelled_tuple:
    case ast_lambda_declaration:
    case ast_lambda_function:
    case ast_let:
    case ast_user_type:
        cat_commentln(self, S("FIXME: generate_expr"));
        return str_copy(self->transient, S("FIXME_generate_expr"));
        break;
    }
}

static void build_arrow_to_c(transpile *, str_build *b, tl_monotype const *type, str name);

static void generate_decl(transpile *self, str name, tl_monotype const *type) {
    if (tl_list == type->tag) {
        // arrow

        str_build b = str_build_init(self->transient, 80);
        build_arrow_to_c(self, &b, type, name);
        cat(self, str_build_finish(&b));
        cat_semicolonln(self);

    }

    else if (tl_cons_inst == type->tag) {
        if (tl_monotype_is_nil(type)) fatal("can't declare a void type");

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    }

    else if (tl_tuple == type->tag) {

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);

    }

    else {
        fatal("got a type variable");
    }
}

static void generate_decl_pointer(transpile *self, str name, tl_monotype const *type) {
    if (tl_list == type->tag) {
        // arrow

        str_build b = str_build_init(self->transient, 80);
        build_arrow_to_c(self, &b, type, name);
        cat(self, str_build_finish(&b));
        cat_semicolonln(self);

    }

    else if (tl_cons_inst == type->tag) {
        if (tl_monotype_is_nil(type)) fatal("can't declare a void type");

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_star(self);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    }

    else if (tl_tuple == type->tag) {

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_star(self);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);

    }

    else {
        fatal("got a type variable");
    }
}

int transpile_compile(transpile *self, str_build *out_build) {

    self->build = str_build_init(self->parent, TRANSPILE_BUILD_SIZE);

    str_build_cat(&self->build, str_init_static(embed_std_c));
    cat_nl(self);
    cat_nl(self);

    generate_user_types(self);
    generate_structs(self);
    generate_toplevel_contexts(self);

    generate_prototypes(self, 1);
    cat_nl(self);

    generate_toplevel_values(self);
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
    transpile *self         = new (alloc, transpile);

    self->parent            = alloc;
    self->arena             = arena_create(alloc, TRANSPILE_ARENA_SIZE);
    self->transient         = arena_create(alloc, TRANSPILE_TRANSIENT_SIZE);

    self->nodes             = opts->infer_result.nodes;
    self->synthesized_nodes = opts->infer_result.synthesized_nodes;
    self->infer             = opts->infer_result.infer;
    self->registry          = opts->infer_result.registry;
    self->env               = opts->infer_result.env;
    self->subs              = opts->infer_result.subs;
    self->toplevels         = opts->infer_result.toplevels;

    self->structs           = hset_create(self->arena, 64);
    self->context_generated = hset_create(self->arena, 64);

    self->next_res          = 0;

    self->verbose           = !!opts->verbose;

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
static void cat_ampersand(transpile *self) {
    cat(self, S("&"));
}
static void cat_assign(transpile *self) {
    cat(self, S(" = "));
}
static void cat_commasp(transpile *self) {
    cat(self, S(", "));
}
static void cat_dot(transpile *self) {
    cat(self, S("."));
}
// static void cat_double_slash(transpile *self) {
//     cat(self, S("// "));
// }
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
static void cat_close_square(transpile *self) {
    cat(self, S("]"));
}
static void cat_close_curlyln(transpile *self) {
    cat(self, S("}\n"));
}
static void cat_semicolon(transpile *self) {
    cat(self, S(";"));
}
static void cat_semicolonln(transpile *self) {
    cat(self, S(";\n"));
}
static void cat_star(transpile *self) {
    cat(self, S("*"));
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
// static void cat_i64(transpile *self, i64 val) {
//     str s = str_init_i64(self->transient, val);
//     cat(self, s);
//     str_deinit(self->transient, &s);
// }
// static void cat_f64(transpile *self, f64 val) {
//     str s = str_init_f64(self->transient, val);
//     cat(self, s);
//     str_deinit(self->transient, &s);
// }

//

static str mangle_fun(transpile *self, str s) {
    // If name is already mangled, it could be a variable name. Don't mangle it further.
    if (0 == str_cmp_nc(s, "tl_", 3)) return s;
    if (0 == str_cmp_nc(s, "std_", 4)) return s;
    str_build b = str_build_init(self->transient, str_len(s) + 7);
    str_build_cat(&b, S("tl_fun_"));
    str_build_cat(&b, s);

    return str_build_finish(&b);
}

static int is_intrinsic(str s) {
    return 0 == str_cmp_nc(s, "_tl_", 4);
}

static int should_generate(str name, tl_polytype const *type) {
    // return 0 if this function should not be generated by transpile
    // during its processing of functions in the environment.
    if (tl_polytype_is_scheme(type)) return 0;
    if (!tl_monotype_is_arrow(type->type)) return 0; // not an arrow
    if (str_eq(name, S("main"))) return 0;
    if (is_intrinsic(name)) return 0;
    return 1;
}

static str type_to_c(transpile *self, tl_polytype const *type) {
    if (type->quantifiers.size) fatal("type scheme");
    tl_monotype const *mono = type->type;
    if (tl_monotype_is_concrete_no_arrow(mono)) {
        str cons_name = mono->cons_inst->def->name;
        if (str_eq(S("Int"), cons_name)) {
            return S("long long");
        } else if (str_eq(S("Float"), cons_name)) {
            return S("double");
        } else if (str_eq(S("Bool"), cons_name)) {
            return S("/*bool*/int");
        } else if (str_eq(S("String"), cons_name)) {
            return S("char const*");
        } else if (str_eq(S("Nil"), cons_name)) {
            return S("void");
        } else if (str_eq(S("Ptr"), cons_name)) {
            assert(1 == mono->cons_inst->args.size);
            tl_monotype const *arg = mono->cons_inst->args.v[0];
            if (tl_monotype_is_concrete(arg)) {
                tl_polytype wrap = tl_polytype_wrap(arg);
                return str_cat(self->transient, type_to_c(self, &wrap), S("*"));
            } else {
                // void*
                return str_init(self->transient, "void*");
            }
        } else {
            if (!str_is_empty(mono->cons_inst->special_name)) return mono->cons_inst->special_name;
            return cons_name;
        }
    }

    else if (tl_monotype_is_arrow(mono))
        fatal("logic error");

    else if (tl_monotype_is_tuple(mono)) {
        str struct_name = make_struct_name(self->transient, mono, null);
        return struct_name;
    }

    else
        fatal("can't render a type variable");
}
static str type_to_c_mono(transpile *self, tl_monotype const *type) {
    tl_polytype wrap = tl_polytype_wrap((tl_monotype *)type);
    return type_to_c(self, &wrap);
}

static str arrow_rhs_to_c(transpile *self, tl_polytype const *type) {
    if (tl_polytype_is_scheme(type)) fatal("type scheme");
    if (!tl_monotype_is_arrow(type->type)) fatal("expected arrow");
    tl_monotype const *right = tl_monotype_sized_last(type->type->list.xs);
    return type_to_c_mono(self, right);
}

static void build_arrow_to_c(transpile *self, str_build *b, tl_monotype const *type, str name) {
    if (!tl_monotype_is_arrow(type)) fatal("logic error");

    if (!tl_monotype_is_arrow(type)) fatal("expected arrow");
    tl_monotype const *right = tl_monotype_sized_last(type->list.xs);
    str_build_cat(b, type_to_c_mono(self, right));
    str_build_cat(b, S(" (*"));
    str_build_cat(b, name);
    str_build_cat(b, S(") ("));

    assert(type->list.xs.size == 2);
    assert(tl_tuple == type->list.xs.v[0]->tag);
    tl_monotype_sized params = type->list.xs.v[0]->list.xs;

    // generate lambda context argument for free variables
    if (type->list.fvs.size) {
        str ctx_name = context_name(self, type->list.fvs);
        str_build_cat(b, ctx_name);
        str_build_cat(b, S("*"));
        if (params.size) str_build_cat(b, S(", "));
    }

    for (u32 i = 0, n = params.size; i < n; ++i) {
        str_build_cat(b, type_to_c_mono(self, type->list.xs.v[i]));
        if (i + 1 < n) str_build_cat(b, S(", "));
    }

    str_build_cat(b, S(")"));
}

static str arrow_to_c_params(transpile *self, tl_polytype const *type, str_sized param_names) {
    // param_names may be empty, e.g. when printing a prototype with no param names.
    if (tl_polytype_is_scheme(type)) fatal("type scheme");
    if (!tl_monotype_is_arrow(type->type)) fatal("expected arrow");

    str_build          b     = str_build_init(self->transient, 64);

    tl_monotype const *arrow = type->type;
    if (tl_list != arrow->tag) fatal("logic error");
    assert(arrow->list.xs.size == 2);
    assert(tl_tuple == arrow->list.xs.v[0]->tag);
    tl_monotype_sized params = arrow->list.xs.v[0]->list.xs;
    assert(!param_names.size || param_names.size == params.size);

    // generate lambda context argument for free variables
    if (arrow->list.fvs.size) {
        str ctx_name = context_name(self, arrow->list.fvs);
        cat(self, ctx_name);
        cat_star(self);

        // always output a name for the context parameter, because we might be in a function definition.
        cat_sp(self);
        cat(self, S("tl_ctx"));
        if (params.size) cat_commasp(self);
    }

    for (u32 i = 0, n = params.size; i < n; ++i) {
        tl_monotype const *arg = params.v[i];
        if (tl_monotype_is_arrow(arg)) {
            build_arrow_to_c(self, &b, arg, (i < param_names.size) ? param_names.v[i] : str_empty());
        } else {
            str_build_cat(&b, type_to_c_mono(self, arg));
            if (i < param_names.size) {
                str_build_cat(&b, S(" "));
                str_build_cat(&b, param_names.v[i]);
            }
        }

        if (i + 1 < n) str_build_cat(&b, S(", "));
    }

    return str_build_finish(&b);
}

tl_monotype const *env_lookup(transpile *self, str name) {
    // may return null if type is missing or is a type scheme
    tl_polytype const *type = tl_type_env_lookup(self->env, name);
    if (!type) fatal("type missing");
    if (tl_polytype_is_scheme(type)) return null;
    return type->type;
}

//

static str tl_sizeof(transpile *self, ast_node const *node, eval_ctx *ctx, void *extra) {
    (void)extra;

    assert(ast_node_is_named_application(node));

    // single argument may be an expression or a type constructor
    if (1 != node->named_application.n_arguments) fatal("wrong number of arguments");
    ast_node const *arg = node->named_application.arguments[0];
    if (ast_node_is_named_application(arg)) {
        // type constructor
        hashmap           *map  = map_new(self->transient, str, tl_type_variable, 8);
        tl_monotype const *type = tl_type_registry_parse(self->registry, arg, self->subs, &map);
        if (!type) fatal("missing type");

        // replace type with its specialized version. tl_infer had no chance to do this because it doesn't
        // know about how to handle _tl_sizeof_'s arguments.
        type = tl_infer_update_specialized_type(self->infer, type);
        if (!type) fatal("missing specialized type");

        str ctype = type_to_c_mono(self, type);
        return str_cat_3(self->transient, S("sizeof("), ctype, S(")"));
    } else {
        // expression
        int save         = ctx->want_lvalue;
        ctx->want_lvalue = 1;
        str expr         = generate_expr(self, null, arg, ctx);
        ctx->want_lvalue = save;
        return str_cat_3(self->transient, S("sizeof("), expr, S(")"));
    }
}

static str generate_funcall_intrinsic(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_node_is_named_application(node));
    str name = ast_node_str(node->named_application.name);

    struct dispatch {
        char const *name;
        str (*fun)(transpile *, ast_node const *, eval_ctx *, void *extra);
        void *extra;
    };

    static const struct dispatch table[] = {
      {"_tl_sizeof_", tl_sizeof, null},
      // {"_tl_sizeoft_", tl_sizeoft, null},

      {"", null, null},
    };

    // NOTE: matches prefix of name limited to length of the defined intrinsics, because inference may
    // replace applications with phantom specialised names, which have a numeric suffix.
    struct dispatch const *p = table;
    for (; p && p->name[0]; ++p)
        if (0 == str_cmp_nc(name, p->name, strlen(p->name))) return p->fun(self, node, ctx, p->extra);

    return str_empty();
}
