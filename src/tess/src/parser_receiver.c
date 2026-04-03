// parser_receiver.c — Receiver block parsing and desugaring.
//
// Syntax:
//   name : TypeExpr : { entries... }
//   name1 : Type1, name2 : Type2 : { entries... }
//
// Each entry inside the block is desugared into a normal top-level function
// with the block's parameters prepended. This is purely a parser transformation.

#include "parser_internal.h"
#include "type_registry.h"

// ============================================================================
// Receiver type expression parser (handles K: HashEq inside brackets)
// ============================================================================

// Forward declaration — mutually recursive with receiver_type_arg.
static int a_receiver_type_expr(parser *self);

// Parse one element inside [...] of a receiver type expression.
// Handles both normal type expressions (Const[String]) and constrained
// type parameters (K: HashEq).
static int receiver_type_arg(parser *self) {
    if (a_try(self, a_receiver_type_expr)) return 1;
    ast_node *node = self->result;
    // Bare symbol followed by ':' → trait constraint (e.g., K: HashEq)
    if (ast_node_is_symbol(node) && 0 == a_try(self, a_colon)) {
        if (a_try(self, a_receiver_type_expr)) return ERROR_STOP;
        node->symbol.annotation = self->result;
        self->result            = node;
    }
    return 0;
}

// Like maybe_type_arguments but uses receiver_type_arg to handle constraints.
static int maybe_receiver_type_arguments(parser *self, ast_node_array *type_args) {
    *type_args = (ast_node_array){.alloc = self->ast_arena};

    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) goto done;
        if (receiver_type_arg(self)) return ERROR_STOP;
        array_push(*type_args, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) goto done;
            if (a_try(self, a_comma)) return ERROR_STOP;
            if (receiver_type_arg(self)) return ERROR_STOP;
            array_push(*type_args, self->result);
        }
    }

done:
    return 0;
}

// Like a_type_identifier_base but uses maybe_receiver_type_arguments.
// Does NOT handle function arrow types or dotted names — receiver types
// are always simple identifiers with optional type arguments.
static int a_receiver_type_expr(parser *self) {
    if (0 == a_try(self, a_attributed_identifier)) {
        ast_node      *ident = self->result;

        ast_node_array type_args;
        if (ERROR_STOP == maybe_receiver_type_arguments(self, &type_args)) return ERROR_STOP;

        mangle_name(self, ident);
        maybe_mangle_implicit_submodule(self, ident);

        if (type_args.size) {
            ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                              (ast_node_sized){0});
            return result_ast_node(self, r);
        } else {
            return result_ast_node(self, ident);
        }
    }

    return 1;
}

// ============================================================================
// Type parameter inference
// ============================================================================

// Check if an identifier name is a known type or module.
static int is_known_type_or_module(parser *self, str name) {
    if (tl_type_registry_get(self->opts.registry, name)) return 1;
    if (str_hset_contains(self->modules_seen, name)) return 1;
    if (self->opts.known_modules && str_map_contains(self->opts.known_modules, name)) return 1;
    if (str_hset_contains(self->builtin_module_symbols, name)) return 1;
    return 0;
}

// Check if a type parameter with this name already exists in the array.
static int has_type_param(ast_node_array const *params, str name) {
    forall(i, *params) {
        if (str_eq(ast_node_str(params->v[i]), name)) return 1;
    }
    return 0;
}

// Recursively walk a type expression AST and collect unknown identifiers
// as type parameters. Constraint annotations (from K: HashEq) are preserved.
static void collect_type_params(parser *self, ast_node *type_expr, ast_node_array *type_params) {
    if (!type_expr) return;

    if (ast_node_is_symbol(type_expr)) {
        // Module-mangled symbols are known identifiers (types or functions from the
        // current module), not type parameters.  mangle_name() only sets this flag
        // for symbols found in current_module_symbols, so it reliably distinguishes
        // concrete names like Foo__Foo from genuine type variables like T or K.
        if (type_expr->symbol.is_module_mangled) return;
        str name = ast_node_str(type_expr);
        if (!is_known_type_or_module(self, name) && !has_type_param(type_params, name)) {
            // Clone the symbol so we have an independent node for the type param list
            ast_node *tp = ast_node_create_sym(self->ast_arena, name);
            set_node_file(self, tp);
            // Preserve trait constraint if present
            if (type_expr->symbol.annotation) {
                tp->symbol.annotation = ast_node_clone(self->ast_arena, type_expr->symbol.annotation);
            }
            array_push(*type_params, tp);
        }
        return;
    }

    if (ast_node_is_nfa(type_expr)) {
        // Don't collect the NFA's name — it's a known type (Ptr, Array, HashMap, etc.)
        // Collect from type arguments (T, K: HashEq, V, Const[String], etc.)
        for (u8 i = 0; i < type_expr->named_application.n_type_arguments; i++) {
            collect_type_params(self, type_expr->named_application.type_arguments[i], type_params);
        }
    }
}

// Deep-clone a type expression, stripping constraint annotations from symbols.
// Constraints belong in the type parameter list, not in function signatures.
static ast_node *clone_type_strip_constraints(allocator *alloc, ast_node const *node) {
    if (!node) return null;

    if (ast_node_is_symbol(node)) {
        ast_node *clone          = ast_node_clone(alloc, node);
        clone->symbol.annotation = null;
        return clone;
    }

    if (ast_node_is_nfa(node)) {
        u8         n    = node->named_application.n_type_arguments;
        ast_node **args = alloc_malloc(alloc, n * sizeof(ast_node *));
        for (u8 i = 0; i < n; i++)
            args[i] = clone_type_strip_constraints(alloc, node->named_application.type_arguments[i]);
        ast_node *name_clone = ast_node_clone(alloc, node->named_application.name);
        return ast_node_create_nfa(alloc, name_clone, (ast_node_sized){.v = args, .size = n},
                                   (ast_node_sized){0});
    }

    return ast_node_clone(alloc, node);
}

// ============================================================================
// Entry parsing
// ============================================================================

// Parse a single function entry inside a receiver block.
// Mirrors toplevel_defun/toplevel_forward but performs no side effects
// (no symbol registration, no name mangling).
static int parse_receiver_entry(parser *self, receiver_entry *out) {
    if (a_try(self, a_attributed_identifier)) return 1;
    out->name        = self->result;

    out->type_params = (ast_node_array){.alloc = self->ast_arena};
    if (maybe_type_parameters(self, &out->type_params)) return 1;

    out->params = (ast_node_array){.alloc = self->ast_arena};
    int res     = parse_param_list(self, &out->params, 1);
    if (res) return res;

    out->return_type = null;
    if (0 == a_try(self, a_arrow)) {
        if (a_try(self, a_type_identifier)) return ERROR_STOP;
        out->return_type = self->result;
    }

    out->body = null;
    if (0 == a_try(self, a_open_curly)) {
        ast_node_array exprs  = {.alloc = self->ast_arena};
        ast_node_array defers = {.alloc = self->ast_arena};
        while (1) {
            if (0 == a_try(self, a_close_curly)) break;
            if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
            else if (a_try(self, a_body_element)) return ERROR_STOP;
            else array_push(exprs, self->result);
        }
        out->body = create_body(self, exprs, defers);
    }

    return 0;
}

// ============================================================================
// Block parsing
// ============================================================================

// Parse a complete receiver block into out. Does not produce AST nodes.
// Returns 0 on success, 1 for backtrack, ERROR_STOP for fatal error.
static int parse_receiver_block(parser *self, receiver_block_info *out) {
    out->params  = (receiver_param_array){.alloc = self->ast_arena};
    out->entries = (receiver_entry_array){.alloc = self->ast_arena};

    // Optionally parse explicit type parameters: [T] or [K: HashEq, V]
    // Only valid for zero-parameter blocks: [T](): { ... }
    if (maybe_type_parameters(self, &out->type_params)) return 1;

    if (a_try(self, a_open_round)) return 1;

    if (out->type_params.size > 0) {
        // [T]() form — require empty param list
        if (a_try(self, a_close_round)) return ERROR_STOP;
    } else if (0 != a_try(self, a_close_round)) {
        // Standard form — parse receiver parameters
        // First parameter: name : TypeExpr
        receiver_param first = {0};
        if (a_try(self, a_identifier)) return 1;
        first.name = self->result;
        if (a_try(self, a_colon)) return 1;
        if (a_try(self, a_receiver_type_expr)) return 1;
        first.type_expr = self->result;
        array_push(out->params, first);

        // Additional comma-separated parameters
        while (0 == a_try(self, a_comma)) {
            receiver_param p = {0};
            if (a_try(self, a_identifier)) return 1;
            p.name = self->result;
            if (a_try(self, a_colon)) return 1;
            if (a_try(self, a_receiver_type_expr)) return 1;
            p.type_expr = self->result;
            array_push(out->params, p);
        }

        // close round
        if (a_try(self, a_close_round)) return 1;
    }

    // Second colon — the disambiguator
    if (a_try(self, a_colon)) return 1;

    // Opening brace — now committed
    if (a_try(self, a_open_curly)) return 1;

    // Parse entries until closing brace
    while (0 != a_try(self, a_close_curly)) {
        receiver_entry entry = {0};
        int            res   = parse_receiver_entry(self, &entry);
        if (res) return ERROR_STOP;
        array_push(out->entries, entry);
    }

    return 0;
}

// ============================================================================
// Desugaring
// ============================================================================

// Desugar a single receiver entry into an AST node.
// For definitions (has body): produces an ast_let (like toplevel_defun).
// For forward declarations (no body): produces an annotated symbol (like toplevel_forward).
static ast_node *desugar_entry(parser *self, receiver_block_info *info, receiver_entry *entry,
                               ast_node_array *block_type_params) {
    allocator *alloc = self->ast_arena;

    // --- Build merged parameter list: receiver params + entry params ---
    ast_node_array full_params = {.alloc = alloc};
    forall(i, info->params) {
        receiver_param *rp = &info->params.v[i];
        // Create a symbol node for the receiver parameter name
        ast_node *param_sym = ast_node_create_sym(alloc, ast_node_str(rp->name));
        set_node_file(self, param_sym);
        // Annotate with the type (constraints stripped — they go in type params)
        param_sym->symbol.annotation = clone_type_strip_constraints(alloc, rp->type_expr);
        array_push(full_params, param_sym);
    }
    forall(i, entry->params) {
        array_push(full_params, entry->params.v[i]);
    }

    // --- Build merged type parameter list: block-level + function-level ---
    ast_node_array full_type_params = {.alloc = alloc};
    forall(i, *block_type_params) {
        array_push(full_type_params, block_type_params->v[i]);
    }
    forall(i, entry->type_params) {
        // Only add if not already present from block-level
        str name = ast_node_str(entry->type_params.v[i]);
        if (!has_type_param(&full_type_params, name)) {
            array_push(full_type_params, entry->type_params.v[i]);
        }
    }

    // --- Build arrow annotation (type signature) ---
    ast_node *name = entry->name;

    if (entry->return_type) {
        ast_node *arrow         = parser_make_arrow(self, full_params, entry->return_type,
                                                    (ast_node_sized)sized_all(full_type_params));

        name->symbol.annotation = arrow;
    }

    // --- Register symbol and mangle name ---
    u8  arity;
    int is_variadic =
      detect_and_register_variadic(self, name, (ast_node_sized)array_sized(full_params), &arity);

    mangle_name(self, name);

    // --- Produce AST node ---
    if (entry->body) {
        // Definition: create ast_let (like toplevel_defun)
        ast_node *let = ast_node_create_let(alloc, name, (ast_node_sized)sized_all(full_type_params),
                                            (ast_node_sized)sized_all(full_params), entry->body);
        set_node_parameters(self, let, &full_params);
        let->let.name        = name;
        let->let.body        = entry->body;
        let->let.is_variadic = is_variadic;
        set_node_file(self, let);
        return let;
    } else {
        // Forward declaration: annotated symbol (like toplevel_forward)
        if (name->symbol.annotation && ast_node_is_arrow(name->symbol.annotation)) {
            ast_node *arrow = name->symbol.annotation;
            array_shrink(full_type_params);
            arrow->arrow.n_type_parameters = (u8)full_type_params.size;
            arrow->arrow.type_parameters   = full_type_params.v;
        }
        return name;
    }
}

// Desugar a receiver_block_info into AST nodes wrapped in an ast_body.
static int desugar_receiver_block(parser *self, receiver_block_info *info) {
    // --- Collect block-level type parameters ---
    ast_node_array block_type_params = {.alloc = self->ast_arena};
    // Explicit type params from [T]() syntax
    forall(i, info->type_params) {
        array_push(block_type_params, info->type_params.v[i]);
    }
    // Inferred type params from receiver param type expressions
    forall(i, info->params) {
        collect_type_params(self, info->params.v[i].type_expr, &block_type_params);
    }

    // --- Desugar each entry ---
    ast_node_array result_nodes = {.alloc = self->ast_arena};
    forall(i, info->entries) {
        ast_node *node = desugar_entry(self, info, &info->entries.v[i], &block_type_params);
        array_push(result_nodes, node);
    }

    array_shrink(result_nodes);

    // Wrap all desugared functions in an ast_body
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)array_sized(result_nodes));
    set_node_file(self, body);
    return result_ast_node(self, body);
}

// ============================================================================
// Top-level entry point
// ============================================================================

// Called from toplevel() dispatch chain via a_try().
int toplevel_receiver_block(parser *self) {
    receiver_block_info info;
    int                 res = parse_receiver_block(self, &info);
    if (res) return res;
    return desugar_receiver_block(self, &info);
}
