// parser_numeric_ops.c — synthesize `cmp` and `eq` functions for builtin numeric
// type families so that `a.cmp(b)` and `a.eq(b)` dispatch through normal UFCS
// resolution for concrete numeric receivers instead of failing "field not found".
//
// One family = one pair of functions. Family names match the `module` field in
// the BUILTIN(...) table at type.c, which is also the key ufcs_rewrite_call uses
// when looking up per-family overloads. Example: all signed integer widths share
// module "Int", so a single Int__cmp__2(a: Int, b: Int) covers CInt, CInt32, ...
//
// Safety: rewrite_operator_overloads in infer_specialize.c skips builtin types
// (is_user_defined_type guard), so `<`, `>`, `==` inside the synthesized bodies
// remain raw C operators and never re-enter trait dispatch.

#include "parser_internal.h"

// Family name matches BUILTIN(...)::module in type.c (also the UFCS lookup key),
// and doubles as the canonical type name for the synthesized parameters.
static char const *const families[] = {
  "Int", "UInt", "Float", "CSize", "CPtrDiff", "CChar", "Bool",
};

static u32 const         n_families = sizeof(families) / sizeof(families[0]);

static char const *const SYNTH_FILE = "<synthesized numeric op>";

static void              stamp(ast_node *node) {
    node->file = SYNTH_FILE;
    node->line = 0;
    node->col  = 0;
}

static ast_node *make_sym(allocator *arena, char const *name) {
    ast_node *n = ast_node_create_sym_c(arena, name);
    stamp(n);
    return n;
}

static ast_node *make_param(allocator *arena, char const *name, char const *type_name) {
    ast_node *p          = make_sym(arena, name);
    p->symbol.annotation = make_sym(arena, type_name);
    return p;
}

// Body: if a < b { -1 } else if a > b { 1 } else { 0 }
static ast_node *build_cmp_body(allocator *arena) {
    // -1 as unary_op(-, 1) — stays safely within inferred integer types.
    ast_node *neg_one =
      ast_node_create_unary_op(arena, make_sym(arena, "-"), ast_node_create_i64(arena, 1));
    stamp(neg_one);
    stamp(neg_one->unary_op.operand);

    ast_node_array then_exprs = {.alloc = arena};
    array_push(then_exprs, neg_one);
    ast_node *then_body = ast_node_create_body(arena, (ast_node_sized)array_sized(then_exprs));
    stamp(then_body);

    ast_node *gt =
      ast_node_create_binary_op(arena, make_sym(arena, ">"), make_sym(arena, "a"), make_sym(arena, "b"));
    stamp(gt);

    ast_node *one_lit = ast_node_create_i64(arena, 1);
    stamp(one_lit);
    ast_node_array inner_then_exprs = {.alloc = arena};
    array_push(inner_then_exprs, one_lit);
    ast_node *inner_then = ast_node_create_body(arena, (ast_node_sized)array_sized(inner_then_exprs));
    stamp(inner_then);

    ast_node *zero_lit = ast_node_create_i64(arena, 0);
    stamp(zero_lit);
    ast_node_array inner_else_exprs = {.alloc = arena};
    array_push(inner_else_exprs, zero_lit);
    ast_node *inner_else = ast_node_create_body(arena, (ast_node_sized)array_sized(inner_else_exprs));
    stamp(inner_else);

    ast_node *inner_if = ast_node_create_if_then_else(arena, gt, inner_then, inner_else);
    stamp(inner_if);

    ast_node_array else_exprs = {.alloc = arena};
    array_push(else_exprs, inner_if);
    ast_node *else_body = ast_node_create_body(arena, (ast_node_sized)array_sized(else_exprs));
    stamp(else_body);

    ast_node *lt =
      ast_node_create_binary_op(arena, make_sym(arena, "<"), make_sym(arena, "a"), make_sym(arena, "b"));
    stamp(lt);

    ast_node *outer_if = ast_node_create_if_then_else(arena, lt, then_body, else_body);
    stamp(outer_if);

    ast_node_array body_exprs = {.alloc = arena};
    array_push(body_exprs, outer_if);
    ast_node *body = ast_node_create_body(arena, (ast_node_sized)array_sized(body_exprs));
    stamp(body);
    return body;
}

// Body: a == b
static ast_node *build_eq_body(allocator *arena) {
    ast_node *eq =
      ast_node_create_binary_op(arena, make_sym(arena, "=="), make_sym(arena, "a"), make_sym(arena, "b"));
    stamp(eq);

    ast_node_array body_exprs = {.alloc = arena};
    array_push(body_exprs, eq);
    ast_node *body = ast_node_create_body(arena, (ast_node_sized)array_sized(body_exprs));
    stamp(body);
    return body;
}

typedef ast_node *(*body_builder)(allocator *);

static ast_node *build_family_let(parser *self, char const *family, char const *fn_name,
                                  char const *ret_type, body_builder build_body) {
    allocator     *arena  = self->ast_arena;

    ast_node_array params = {.alloc = arena};
    ast_node      *p_a    = make_param(arena, "a", family);
    ast_node      *p_b    = make_param(arena, "b", family);
    array_push(params, p_a);
    array_push(params, p_b);

    // parser_make_arrow would stamp with stale token state (we are past EOF);
    // build and stamp directly with the synthesized location instead.
    ast_node *tup = ast_node_create_tuple(arena, (ast_node_sized)array_sized(params));
    stamp(tup);
    ast_node *arrow = ast_node_create_arrow(arena, tup, make_sym(arena, ret_type), (ast_node_sized){0});
    stamp(arrow);

    ast_node *func_name = ast_node_create_sym_c(arena, fn_name);
    ast_node_name_replace(func_name, mangle_str_for_arity(arena, func_name->symbol.name, 2));
    func_name->symbol.annotation = arrow;
    stamp(func_name);
    mangle_name_for_module(self, func_name, str_init(arena, family));

    ast_node *let =
      ast_node_create_let(arena, func_name, (ast_node_sized){0},
                          (ast_node_sized)array_sized(params), build_body(arena));
    stamp(let);
    return let;
}

// Check whether the user has defined <module>__<fn>__2 themselves, by looking
// at the transplanted module symbol table. The table contains arity-mangled
// names; module-prefix mangling is applied later during inference via the let's
// symbol.module. So the lookup key is just "<fn>__<arity>".
static int user_has_definition(parser *self, char const *module, char const *fn_name) {
    str      module_str = str_init(self->transient, module);
    hashmap *syms       = resolve_module_symbols(self, module_str);
    if (!syms) return 0;
    str mangled = mangle_str_for_arity(self->transient, str_init(self->transient, fn_name), 2);
    return str_hset_contains(syms, mangled);
}

// Emit synthesized let nodes onto the toplevel stream. Skips a family when the
// user has defined <family>__<fn>__2 so user definitions always win.
void parser_synthesize_builtin_numeric_lets(parser *self, ast_node_array *out) {
    for (u32 i = 0; i < n_families; i++) {
        char const *family = families[i];
        if (!user_has_definition(self, family, "cmp")) {
            ast_node *let = build_family_let(self, family, "cmp", "CInt", build_cmp_body);
            array_push(*out, let);
        }
        if (!user_has_definition(self, family, "eq")) {
            ast_node *let = build_family_let(self, family, "eq", "Bool", build_eq_body);
            array_push(*out, let);
        }
    }
}
