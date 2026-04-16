// Synthesize `cmp` and `eq` UFCS methods for builtin numeric families so
// `a.cmp(b)` resolves without the user defining anything.
//
// Generic [T] (not a concrete type) is load-bearing: one family ("Int") covers
// multiple subchains (c-width CInt/CLongLong and fixed-width CInt8..CInt64)
// that do not unify with any single concrete parameter type. The body calls a
// type-erased C helper in builtin.tl with `sizeof(T)` — a per-specialization
// constant, so the switch inside is folded at -O1+.

#include "parser_internal.h"

typedef struct {
    char const *family;     // UFCS module key (from BUILTIN(...).module in type.c)
    char const *cmp_helper; // C helper for cmp; NULL skips cmp synthesis (e.g. Bool)
    char const *eq_helper;  // C helper for eq
} family_info;

static family_info const families[] = {
  {"Int", "c_tl_cmp_signed", "c_tl_eq_bytes"},
  {"UInt", "c_tl_cmp_unsigned", "c_tl_eq_bytes"},
  {"Float", "c_tl_cmp_float", "c_tl_eq_float"},
  {"CSize", "c_tl_cmp_unsigned", "c_tl_eq_bytes"},
  {"CPtrDiff", "c_tl_cmp_signed", "c_tl_eq_bytes"},
  {"CChar", "c_tl_cmp_signed", "c_tl_eq_bytes"},
  {"Bool", null, "c_tl_eq_bytes"},
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

// Build an NFA with an arity-mangled name, mirroring the parser's normal
// call-site handling (mangle_name_for_arity with is_definition=0). Needed for
// builtin functions like `sizeof` that live in #module builtin and are looked
// up by the arity-mangled form. C-prefixed symbols (`c_*`) are left alone.
static ast_node *build_mangled_call(parser *self, allocator *arena, char const *name, ast_node_array args) {
    ast_node *fn = make_sym(arena, name);
    mangle_name_for_arity(self, fn, (u8)args.size, 0);
    ast_node *nfa = ast_node_create_nfa(arena, fn, (ast_node_sized){0}, (ast_node_sized)array_sized(args));
    stamp(nfa);
    return nfa;
}

// Body: helper(a.&, b.&, sizeof(a))
static ast_node *build_helper_call_body(parser *self, allocator *arena, char const *helper_name) {
    ast_node *addr_a = ast_node_create_unary_op(arena, make_sym(arena, "&"), make_sym(arena, "a"));
    stamp(addr_a);
    ast_node *addr_b = ast_node_create_unary_op(arena, make_sym(arena, "&"), make_sym(arena, "b"));
    stamp(addr_b);

    ast_node_array sizeof_args = {.alloc = arena};
    ast_node      *sizeof_arg  = make_sym(arena, "a");
    array_push(sizeof_args, sizeof_arg);
    ast_node      *sizeof_call = build_mangled_call(self, arena, "sizeof", sizeof_args);

    ast_node_array call_args   = {.alloc = arena};
    array_push(call_args, addr_a);
    array_push(call_args, addr_b);
    array_push(call_args, sizeof_call);
    ast_node      *call       = build_mangled_call(self, arena, helper_name, call_args);

    ast_node_array body_exprs = {.alloc = arena};
    array_push(body_exprs, call);
    ast_node *body = ast_node_create_body(arena, (ast_node_sized)array_sized(body_exprs));
    stamp(body);
    return body;
}

static ast_node *build_family_let(parser *self, char const *family, char const *fn_name,
                                  char const *ret_type, char const *helper_name) {
    allocator     *arena       = self->ast_arena;

    ast_node_array type_params = {.alloc = arena};
    ast_node      *t_param     = make_sym(arena, "T");
    array_push(type_params, t_param);

    ast_node_array params = {.alloc = arena};
    ast_node      *p_a    = make_param(arena, "a", "T");
    ast_node      *p_b    = make_param(arena, "b", "T");
    array_push(params, p_a);
    array_push(params, p_b);

    // parser_make_arrow would stamp with stale token state (we are past EOF);
    // build and stamp directly with the synthesized location instead.
    ast_node *tup = ast_node_create_tuple(arena, (ast_node_sized)array_sized(params));
    stamp(tup);
    ast_node *arrow = ast_node_create_arrow(arena, tup, make_sym(arena, ret_type),
                                            (ast_node_sized)array_sized(type_params));
    stamp(arrow);

    ast_node *func_name = ast_node_create_sym_c(arena, fn_name);
    ast_node_name_replace(func_name, mangle_str_for_arity(arena, func_name->symbol.name, 2));
    func_name->symbol.annotation = arrow;
    stamp(func_name);
    mangle_name_for_module(self, func_name, str_init(arena, family));

    ast_node *let = ast_node_create_let(arena, func_name, (ast_node_sized)array_sized(type_params),
                                        (ast_node_sized)array_sized(params),
                                        build_helper_call_body(self, arena, helper_name));
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

// Emit synthesized let nodes at the FRONT of the toplevel stream. Prepending is
// load-bearing: the inferrer generalizes each let's polytype after processing
// its body, so the synth bodies must be inferred before any user code that
// calls them — otherwise user call sites bind the yet-ungeneralized shared TVs
// and cross-type calls in one function collide (e.g. CFloat.cmp then CDouble.cmp).
//
// Skips a family when the user has defined <family>__<fn>__2 so user
// definitions always win.
void parser_synthesize_builtin_numeric_lets(parser *self, ast_node_array *out) {
    ast_node_array synth = {.alloc = self->ast_arena};
    for (u32 i = 0; i < n_families; i++) {
        family_info const *fi = &families[i];
        if (fi->cmp_helper && !user_has_definition(self, fi->family, "cmp")) {
            ast_node *let = build_family_let(self, fi->family, "cmp", "CInt", fi->cmp_helper);
            array_push(synth, let);
        }
        if (fi->eq_helper && !user_has_definition(self, fi->family, "eq")) {
            ast_node *let = build_family_let(self, fi->family, "eq", "Bool", fi->eq_helper);
            array_push(synth, let);
        }
    }
    if (synth.size) array_insert(*out, 0, synth.v, synth.size);
}
