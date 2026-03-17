// infer_update.c — Phases 6-7: Tree Shaking, Type Updates & Invariants
//
// Phase 6: Starting from main (or all exports in library mode), walks the call
// graph via DFS and collects reachable names.  tree_shake_toplevels prunes
// unreachable definitions.
//
// Phase 7: After specialization and tree shaking, replaces generic type
// constructors in the final AST with their specialized versions and checks for
// any remaining unresolved type variables.
//
// Also contains DEBUG_INVARIANTS functions for phase-boundary validation.

#include "infer_internal.h"

// ============================================================================
// Tree shaking (Phase 6)
// ============================================================================

typedef struct {
    tl_infer *self;
    hashmap  *names;  // str set
    hashmap  *recurs; // str set
} tree_shake_ctx;

static void do_tree_shake(void *, ast_node *);

// Helper for tree shaking value bindings (let_in and reassignment)
static void tree_shake_value_binding(tree_shake_ctx *ctx, ast_node *value) {
    if (!value) return;

    tl_infer *self = ctx->self;

    if (ast_node_is_symbol(value)) {
        str name = ast_node_str(value);

        // if it is a toplevel, recurse through it
        ast_node *next = toplevel_get(self, name);
        if (next) ast_node_dfs(ctx, next, do_tree_shake);
        str_hset_insert(&ctx->recurs, name);
        str_hset_insert(&ctx->names, name);
    } else {
        // recurse into value
        ast_node_dfs(ctx, value, do_tree_shake);
    }
}

static void do_tree_shake(void *ctx_, ast_node *node) {
    tree_shake_ctx *ctx  = ctx_;
    tl_infer       *self = ctx->self;

    if (ast_node_is_nfa(node)) {
        str name = toplevel_name(node);

        str_hset_insert(&ctx->names, name);

        // add all symbol arguments because they could be function pointers
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg)) arg = arg->assignment.value;
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(name, arg->symbol.name)) continue;
            if (!str_hset_contains(ctx->recurs, arg->symbol.name)) {
                str_hset_insert(&ctx->recurs, arg->symbol.name);

                // if it is a toplevel, recurse through it
                ast_node *next = toplevel_get(self, arg->symbol.name);
                if (next) {
                    ast_node_dfs(ctx, next, do_tree_shake);

                    // and save the name
                    str_hset_insert(&ctx->names, arg->symbol.name);
                }
            }
        }

        if (!str_hset_contains(ctx->recurs, name)) {
            str_hset_insert(&ctx->recurs, name);

            ast_node *next = toplevel_get(ctx->self, name);
            if (next) {
                ast_node_dfs(ctx, next, do_tree_shake);
            }

            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_let_in(node)) {
        tree_shake_value_binding(ctx, node->let_in.value);

        // the let-in name
        str name = ast_node_str(node->let_in.name);
        str_hset_insert(&ctx->names, name);
    } else if (ast_node_is_reassignment(node)) {
        tree_shake_value_binding(ctx, node->assignment.value);
    }

    else if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *param;
        while ((param = ast_arguments_next(&iter))) {
            if (!ast_node_is_symbol(param)) continue;
            str name = ast_node_str(param);
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_case == node->tag && node->case_.binary_predicate) {
        ast_node *pred = node->case_.binary_predicate;
        if (ast_node_is_symbol(pred)) {
            str name = ast_node_str(pred);
            str_hset_insert(&ctx->names, name);
        }
    } else if (ast_node_is_symbol(node)) {
        // Handle bare symbols that may reference toplevel functions (e.g., function pointers in case arms)
        str       name = ast_node_str(node);
        ast_node *next = toplevel_get(self, name);
        if (next && !str_hset_contains(ctx->recurs, name)) {
            str_hset_insert(&ctx->recurs, name);
            ast_node_dfs(ctx, next, do_tree_shake);
            str_hset_insert(&ctx->names, name);
        }
    }
}

hashmap *tree_shake(tl_infer *self, ast_node const *node) {
    tree_shake_ctx ctx = {.self = self};
    ctx.names          = hset_create(self->transient, 1024);
    ctx.recurs         = hset_create(self->transient, 1024);

    str_hset_insert(&ctx.names, toplevel_name(node));

    ast_node_dfs(&ctx, (ast_node *)node, do_tree_shake);

    return ctx.names;
}

void tree_shake_toplevels(tl_infer *self, ast_node const *start) {
    hashmap  *used   = tree_shake(self, start);

    str_array remove = {.alloc = self->transient};

    // Add all toplevel let-in names (globals) and type names because we now use this process to determine
    // all symbols used in the program. This helps us identify nonexistent free variables.
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_let_in(node) && !ast_node_is_let_in_lambda(node)) {
            str name = ast_node_str(node->let_in.name);
            str_hset_insert(&used, name);

            // recurse into toplevel let-in nodes
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);

        } else if (ast_node_is_utd(node)) {
            str name = ast_node_str(node->user_type_def.name);
            str_hset_insert(&used, name);
        }

        // Note: special case: preserve module init functions.
        else if (ast_node_is_let(node) && is_module_init(ast_node_str(node->let.name))) {
            str_hset_insert(&used, node->let.name->symbol.name);

            // recurse into toplevel module init functions
            hashmap *recur = tree_shake(self, node);
            map_merge(&used, recur);
            map_destroy(&recur);
        }
    }

    iter = (hashmap_iterator){0};
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;

        // preserve value let-ins, but not unused let-in-lambdas (including the latter causes a test
        // failure)
        if (ast_node_is_let_in(node) && !ast_node_is_let_in_lambda(node)) continue;

        str name = toplevel_name(node);
        if (!str_hset_contains(used, name)) array_push(remove, name);
    }

    forall(i, remove) {
        dbg_at(2, self, "tree_shake_toplevels: removing '%s'", str_cstr(&remove.v[i]));
        toplevel_del(self, remove.v[i]);
    }
    array_free(remove);

    // Note: also remove any unused name in the environment
    tl_type_env_remove_unknown_symbols(self->env, used);

    map_destroy(&used);
}

// ============================================================================
// Type specialization updates (Phase 7)
// ============================================================================

tl_monotype *tl_infer_update_specialized_type_(tl_infer *self, tl_monotype *mono, hashmap **in_progress) {

    // Note: this function pretty definitely breaks the isolation between tl_infer and the transpiler so
    // that makes me a little bit sad. But it makes sizeof(TypeConstructor) work.

    switch (mono->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:        break;

    case tl_cons_inst:         {

        int did_replace  = !tl_monotype_is_inst_specialized(mono);
        str generic_name = mono->cons_inst->def->generic_name;

        // check args recursively
        str_hset_insert(in_progress, generic_name);
        forall(i, mono->cons_inst->args) {
            tl_monotype *arg = mono->cons_inst->args.v[i];
            if (!tl_monotype_is_inst(arg) ||
                !str_hset_contains(*in_progress, arg->cons_inst->def->generic_name)) {

                tl_monotype *replace = tl_infer_update_specialized_type_(self, arg, in_progress);

                if (replace) {
                    mono->cons_inst->args.v[i] = replace;
                    did_replace                = 1;
                }
            }
        }
        str_hset_remove(*in_progress, generic_name);
        if (!did_replace) {
            if (self->report_stats) self->counters.update_types_type_cons_skipped++;
            return null;
        }

        tl_polytype *replace = null;
        if (self->report_stats) self->counters.update_types_type_cons_calls++;
        (void)specialize_type_constructor(self, mono->cons_inst->def->generic_name, mono->cons_inst->args,
                                          &replace);

#if DEBUG_EXPLICIT_TYPE_ARGS
        {
            str gn = mono->cons_inst->def->generic_name;
            fprintf(stderr, "[DEBUG UPDATE_SPECIALIZED] specialize_type_constructor('%s'):\n",
                    str_cstr(&gn));
            fprintf(stderr, "  replace = %p\n", (void *)replace);
            if (replace) {
                str ts = tl_monotype_to_string(self->transient, replace->type);
                fprintf(stderr, "  replace->type = %s\n", str_cstr(&ts));
                fprintf(stderr, "  is_specialized = %d\n", tl_monotype_is_inst_specialized(replace->type));
            }
        }
#endif

        if (replace && !tl_monotype_is_inst_specialized(replace->type)) fatal("unreachable");

        if (replace && did_replace) {
            return replace->type;
        } else {
            return null;
        }

    } break;

    case tl_arrow:
    case tl_tuple: {
        int did_replace = 0;
        forall(i, mono->list.xs) {
            tl_monotype *replace = tl_infer_update_specialized_type_(self, mono->list.xs.v[i], in_progress);
            if (replace) {
                mono->list.xs.v[i] = replace;
                did_replace        = 1;
            }
        }
        if (did_replace) return mono;

    } break;
    }

    return null;
}

tl_monotype *tl_infer_update_specialized_type(tl_infer *self, tl_monotype *mono) {
    switch (mono->tag) {
    case tl_var:               tl_monotype_substitute(self->arena, mono, self->subs, null); break;

    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:
    case tl_placeholder:       return null;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:             {
        hashmap     *in_progress = hset_create(self->transient, 64);
        tl_monotype *out         = tl_infer_update_specialized_type_(self, mono, &in_progress);
        return out;
    }
    }
    return null;
}

typedef struct {
    hashmap *in_progress;
} update_types_ctx;

static void update_types_one_type(tl_infer *self, update_types_ctx *ctx, tl_polytype **poly) {
    if (!poly || !*poly) return; // not all ast nodes will have types

    // Don't try to specialize type schemes
    if (tl_polytype_is_scheme(*poly)) return;

    switch ((*poly)->type->tag) {
    case tl_any:
    case tl_ellipsis:
    case tl_integer:
    case tl_var:
    case tl_weak:
    case tl_weak_int_signed:
    case tl_weak_int_unsigned:
    case tl_weak_float:
    case tl_placeholder:       return;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:             {
        // For recursive types, bounce until no changes. update_specialized_type returns null if there is no
        // need to replace the type being tested.
        int tries = 3;
        while (tries--) {
            int did_replace = 1;

            hset_reset(ctx->in_progress);
            tl_monotype *replace =
              tl_infer_update_specialized_type_(self, (*poly)->type, &ctx->in_progress);

            if (replace) *poly = tl_polytype_absorb_mono(self->arena, replace);
            else did_replace = 0;

            if (!did_replace) break;
        }
        if (-1 == tries) fatal("loop exhausted");
    }
    }
}

static void fixup_arrow_name(tl_infer *self, ast_node *ident) {
    if (ast_node_is_symbol(ident)) {
        tl_monotype *type = ident->type->type;
        if (!tl_monotype_is_arrow(type)) return;
        str name = ast_node_str(ident);

        // TODO: function pointers with type arguments
        str *inst_name = instance_lookup_arrow(self, name, type, (tl_monotype_sized){0});
        if (inst_name) ast_node_name_replace(ident, *inst_name);
    }
}

static void update_types_arrow(tl_infer *self, ast_node *node) {
    if (ast_node_is_let_in(node)) {
        ast_node *ident = node->let_in.value;
        fixup_arrow_name(self, ident);
    }
}

static int update_types_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    update_types_ctx *ctx = traverse_ctx->user;
    update_types_one_type(self, ctx, &node->type);
    update_types_arrow(self, node);

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

    // propagate the types back up the ast, especially for type constructors
    switch (node->tag) {
    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:      ast_node_type_set(node, node->assignment.value->type); break;

    case ast_body:            {
        u32 n = node->body.expressions.size;
        if (n) ast_node_type_set(node, node->body.expressions.v[n - 1]->type);
    } break;

    case ast_let_in:
        if (node->let_in.body) ast_node_type_set(node, node->let_in.body->type);

        // Note: ensure name's type in the environment matches a specialized type constructor on the rhs
        {
            tl_monotype *value_type = node->let_in.value->type->type;

            if (tl_monotype_is_inst_specialized(value_type)) {
                tl_polytype *new_type = tl_polytype_absorb_mono(self->arena, value_type);
                ast_node_type_set(node->let_in.name, new_type);
                tl_type_env_insert(self->env, ast_node_str(node->let_in.name), new_type);
            }
        }
        break;

    default: break;
    }

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif
    return 0;
}

void update_specialized_types(tl_infer *self) {
    update_types_ctx ctx = {.in_progress = hset_create(self->transient, 64)};
    hires_timer      ut;
    int              stats = self->report_stats;

    if (stats) {
        hires_timer_init(&ut);
        hires_timer_start(&ut);
    }

    // Snapshot the env keys before iterating. update_types_one_type may trigger
    // specialize_type_constructor_ which inserts new entries into the env. Robin Hood
    // hashing can relocate existing entries on insertion, invalidating any data pointers
    // obtained from a prior map_iter call.
    str_array env_keys = str_map_keys(self->transient, self->env->map);
    if (stats) self->counters.update_types_env_count = env_keys.size;
    forall(ki, env_keys) {
        tl_polytype *poly = tl_type_env_lookup(self->env, env_keys.v[ki]);
        if (!poly) continue;
        tl_polytype *orig = poly;
        update_types_one_type(self, &ctx, &poly);
        if (poly != orig) tl_type_env_insert(self->env, env_keys.v[ki], poly);
    }
    array_free(env_keys);

    if (stats) {
        hires_timer_stop(&ut);
        self->counters.update_types_env_ms = hires_timer_elapsed_sec(&ut) * 1000.0;
    }

    // NOTE: this is an expensive traverse
    if (stats) hires_timer_start(&ut);
    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    traverse->user         = &ctx;
    hashmap_iterator iter  = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        if (stats) self->counters.traverse_update_types_calls++;
        traverse_ast(self, traverse, node, update_types_cb);
        // Note: traverse_ast does not traverse let nodes directly (just their sub-parts)
    }
    if (stats) {
        hires_timer_stop(&ut);
        self->counters.update_types_ast_ms = hires_timer_elapsed_sec(&ut) * 1000.0;
    }
    arena_reset(self->transient);
}

static int check_unresolved_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (traverse_ctx->is_field_name) return 0;

    if (ast_node_is_let_in(node) && !tl_monotype_is_arrow(node->let_in.value->type->type)) {
        if (!tl_polytype_is_concrete(node->let_in.name->type)) {
            // try substitute again before error
            tl_polytype_substitute(self->arena, node->let_in.name->type, self->subs);
            if (!tl_polytype_is_concrete(node->let_in.name->type))
                unresolved_type_error(self, node->let_in.name);
        }
    } else if (ast_node_is_reassignment(node) && !tl_polytype_is_concrete(node->type)) {
        // try substitute again before error
        tl_polytype_substitute(self->arena, node->type, self->subs);
        if (!tl_polytype_is_concrete(node->type)) unresolved_type_error(self, node);
    }

    return 0;
}

void check_unresolved_types(tl_infer *self) {
    // checks if any nodes in ast are still type variables
    traverse_ctx    *traverse = traverse_ctx_create(self->transient);
    hashmap_iterator iter     = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;
        traverse_ast(self, traverse, node, check_unresolved_cb);
    }
    arena_reset(self->transient);
}

// ============================================================================
// Closure escape checking
// ============================================================================

// Check whether a monotype is a capturing arrow (has free variables).
static int is_capturing_arrow(tl_monotype *type) {
    return tl_monotype_is_arrow(type) && type->list.fvs.size > 0;
}

// Check if an expression node has a type that is a capturing arrow.
// Called after update_specialized_types, so node->type->type is authoritative.
static int node_is_capturing_closure(ast_node const *node) {
    if (!node || !node->type || !node->type->type) return 0;
    return is_capturing_arrow(node->type->type);
}

static void closure_escape_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_closure_escape, .node = node}));
}

// Check whether a node (at an escape point) is an allocated closure.
// Traces symbols back through let_in bindings to find the originating lambda.
static int is_allocated_closure(tl_infer *self, ast_node *node, hashmap *bindings) {
    if (!node) return 0;

    // Direct lambda
    if (node->tag == ast_lambda_function) return lambda_has_alloc(self, node);

    // Symbol — look up its binding in the let_in map and follow alias chains
    if (ast_node_is_symbol(node) && bindings) {
        ast_node *value = ast_node_str_map_get(bindings, ast_node_str(node));
        if (value) return is_allocated_closure(self, value, bindings);
    }

    // let_in — record the binding and trace into the body.
    // Pattern: f := lambda; f  =>  let_in(f, lambda, body([f]))
    if (ast_node_is_let_in(node)) {
        if (!bindings) bindings = ast_node_str_map_create(self->transient, 16);
        ast_node_str_map_add(&bindings, ast_node_str(node->let_in.name), node->let_in.value);
        return is_allocated_closure(self, node->let_in.body, bindings);
    }

    // body — trace to the last expression (the implicit return value)
    if (ast_node_is_body(node) && node->body.expressions.size > 0) {
        ast_node *last = node->body.expressions.v[node->body.expressions.size - 1];
        return is_allocated_closure(self, last, bindings);
    }

    // if/else — both branches must be allocated closures
    if (node->tag == ast_if_then_else) {
        return is_allocated_closure(self, node->if_then_else.yes, bindings) &&
               is_allocated_closure(self, node->if_then_else.no, bindings);
    }

    return 0;
}

typedef struct {
    hashmap *bindings; // str -> ast_node* (symbol name -> let_in value)
} closure_escape_walk_ctx;

static int check_closure_escape_cb(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    closure_escape_walk_ctx *esc_ctx = ctx->user;

    // Track let_in bindings for symbol-to-lambda tracing
    if (ast_node_is_let_in(node))
        ast_node_str_map_add(&esc_ctx->bindings, ast_node_str(node->let_in.name), node->let_in.value);

    // Check struct construction: NFA with is_type_constructor flag
    if (ast_node_is_nfa(node) && node->named_application.is_type_constructor) {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg) && node_is_capturing_closure(arg->assignment.value)) {
                if (!is_allocated_closure(self, arg->assignment.value, esc_ctx->bindings))
                    closure_escape_error(self, arg->assignment.value);
            }
        }
    }

    // Check explicit return of capturing closure
    if (node->tag == ast_return && node->return_.value) {
        if (node_is_capturing_closure(node->return_.value)) {
            if (!is_allocated_closure(self, node->return_.value, esc_ctx->bindings))
                closure_escape_error(self, node->return_.value);
        }
    }

    return 0;
}

// Get the body's last expression (implicit return value) for a toplevel function.
static ast_node *toplevel_implicit_return(ast_node *node) {
    ast_node *body = ast_node_body(node);
    if (!body || !ast_node_is_body(body)) return null;
    if (!body->body.expressions.size) return null;
    return body->body.expressions.v[body->body.expressions.size - 1];
}

// ============================================================================
// Closure alloc/capture attribute validation
// ============================================================================

// Capture names are alpha-converted during Phase 1 (infer_alpha.c).  Names that were in
// lexical scope get renamed (e.g. n → tl_n_v36); names NOT in scope are left unchanged
// (their symbol.original remains empty).  This lets us detect not-in-scope errors here
// by checking whether a capture symbol was alpha-converted.

// Collect free variables from a lambda body into a str_array, registering lambda parameters
// as lexical names so they are excluded.
static str_array collect_lambda_fvs(tl_infer *self, ast_node *node) {
    collect_free_variables_ctx fv_ctx;
    fv_ctx.fvs             = (str_array){.alloc = self->transient};

    traverse_ctx *inner    = traverse_ctx_create(self->transient);
    inner->user            = &fv_ctx;
    inner->skip_alloc_expr = 1; // alloc_expr is not part of the closure body

    for (u8 i = 0; i < node->lambda_function.n_parameters; i++)
        str_hset_insert(&inner->lexical_names, ast_node_str(node->lambda_function.parameters[i]));

    traverse_ast(self, inner, node->lambda_function.body, collect_free_variables_cb);
    return fv_ctx.fvs;
}

// Combined traversal callback: validates alloc/capture attribute combinations on lambdas.
// For [[alloc]] without [[capture(...)]]: reports alloc_missing_capture if free vars exist.
// For [[alloc, capture(...)]]: validates capture list matches detected free variables exactly.
// Capture names are alpha-converted (Phase 1), so they can be compared directly against
// alpha-converted free variable names from collect_free_variables_cb.
static int check_closure_attrs_cb(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    (void)ctx;
    if (node->tag != ast_lambda_function) return 0;
    if (!node->lambda_function.attributes) return 0;

    lambda_closure_attrs attrs =
      lambda_get_closure_attrs(self->transient, node->lambda_function.attributes);
    if (!attrs.has_alloc) return 0;

    // Validate alloc_expr type is Ptr[Allocator].
    if (attrs.alloc_expr && attrs.alloc_expr->type) {
        tl_polytype_substitute(self->arena, attrs.alloc_expr->type, self->subs);
        tl_monotype *resolved = attrs.alloc_expr->type->type;
        if (!tl_monotype_is_ptr(resolved) ||
            !tl_monotype_is_inst_of(tl_monotype_ptr_target(resolved), S("Alloc__Allocator"))) {
            array_push(self->errors,
                       ((tl_infer_error){.tag = tl_err_alloc_expr_type_mismatch, .node = node}));
            return 0;
        }
    }

    // [[alloc]] without [[capture(...)]]: check whether the body has free variables.
    if (!attrs.has_capture) {
        str_array fvs = collect_lambda_fvs(self, node);
        if (fvs.size)
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_alloc_missing_capture, .node = node}));
        return 0;
    }

    // [[alloc, capture(...)]]: first check that all capture names are in scope.
    // A capture name that was NOT alpha-converted (symbol.original is empty) was not
    // found in lexical scope during Phase 1.
    for (u8 j = 0; j < attrs.n_capture_names; j++) {
        ast_node *cap = attrs.capture_nodes[j];
        if (ast_node_is_symbol(cap) && str_is_empty(cap->symbol.original)) {
            array_push(self->errors,
                       ((tl_infer_error){
                         .tag = tl_err_capture_not_in_scope, .node = node, .message = cap->symbol.name}));
            return 0;
        }
    }

    // Validate capture list matches free variables exactly.
    str_array fvs = collect_lambda_fvs(self, node);

    // Check that every detected free variable is listed in capture(...).
    forall(i, fvs) {
        str fv    = fvs.v[i];
        int found = 0;
        for (u8 j = 0; j < attrs.n_capture_names; j++) {
            if (str_eq(fv, attrs.capture_names[j])) {
                found = 1;
                break;
            }
        }
        if (!found) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_capture_unlisted_var, .node = node}));
            return 0;
        }
    }

    // Check that every name in capture(...) is a detected free variable.
    for (u8 j = 0; j < attrs.n_capture_names; j++) {
        str cap   = attrs.capture_names[j];
        int found = 0;
        forall(i, fvs) {
            if (str_eq(cap, fvs.v[i])) {
                found = 1;
                break;
            }
        }
        if (!found) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_capture_unused_var, .node = node}));
            return 0;
        }
    }

    return 0;
}

// Combined callback: runs both closure escape and alloc/capture checks in a single traversal.
static int check_closure_checks_cb(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    check_closure_escape_cb(self, ctx, node);
    check_closure_attrs_cb(self, ctx, node);
    return 0;
}

void check_closure_checks(tl_infer *self) {
    traverse_ctx           *traverse = traverse_ctx_create(self->transient);
    hashmap_iterator        iter     = {0};
    ast_node               *node;

    closure_escape_walk_ctx esc_ctx = {0};
    traverse->user                  = &esc_ctx;

    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue;

        // Reset per-function binding map so bindings from one function don't leak into another.
        esc_ctx.bindings = ast_node_str_map_create(self->transient, 16);

        // Check implicit return: top-level function's body last expression.
        // Skip ast_return nodes — those are caught by the traversal callback.
        ast_node *last = toplevel_implicit_return(node);
        if (last && last->tag != ast_return && node_is_capturing_closure(last)) {
            if (!is_allocated_closure(self, last, esc_ctx.bindings)) closure_escape_error(self, last);
        }

        // Single traversal for both escape and alloc/capture checks
        traverse_ast(self, traverse, node, check_closure_checks_cb);
        if (self->errors.size) return;
    }
}

// ============================================================================
// Invariant checking (debug-only)
// ============================================================================

#if DEBUG_INVARIANTS

void report_invariant_failure(tl_infer *self, char const *phase, char const *invariant, char const *detail,
                              ast_node const *node) {
    fprintf(stderr, "\nINVARIANT VIOLATION [%s]\n", phase);
    fprintf(stderr, "  Invariant: %s\n", invariant);
    fprintf(stderr, "  Detail:    %s\n", detail);
    if (node) {
        fprintf(stderr, "  Location:  %s:%u\n", node->file, node->line);
        str s = ast_node_to_short_string(self->transient, node);
        fprintf(stderr, "  Node:      %.*s\n", str_ilen(s), str_buf(&s));
        str_deinit(self->transient, &s);
    }
    fprintf(stderr, "\n");
}

void check_types_null_cb(void *ctx_ptr, ast_node *node) {
    struct check_types_null_ctx *ctx = ctx_ptr;
    if (node->type != null) {
        char detail[256];
        snprintf(detail, sizeof detail, "Node has non-null type at %s:%u", node->file, node->line);
        report_invariant_failure(ctx->self, ctx->phase, "All AST node types must be null", detail, node);
        ctx->failures++;
    }
}

int check_all_types_null(tl_infer *self, ast_node_sized nodes, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    forall(i, nodes) {
        ast_node_dfs(&ctx, nodes.v[i], check_types_null_cb);
    }
    return ctx.failures;
}

void check_type_arg_types_null_one(struct check_types_null_ctx *ctx, ast_node *node) {
    // Check type parameters on let nodes
    if (ast_node_is_let(node)) {
        struct ast_let *let = &node->let;
        for (u8 i = 0; i < let->n_type_parameters; ++i) {
            if (let->type_parameters[i] && let->type_parameters[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "Type parameter %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type parameter types must be null", detail,
                                         let->type_parameters[i]);
                ctx->failures++;
            }
        }
    }
    // Check type arguments on named function applications
    else if (ast_node_is_nfa(node)) {
        struct ast_named_application *nfa = &node->named_application;
        for (u8 i = 0; i < nfa->n_type_arguments; ++i) {
            if (nfa->type_arguments[i] && nfa->type_arguments[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "Type argument %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type argument types must be null", detail,
                                         nfa->type_arguments[i]);
                ctx->failures++;
            }
        }
    }
    // Check type arguments on user type definitions
    else if (ast_node_is_utd(node)) {
        struct ast_user_type_def *utd = &node->user_type_def;
        for (u8 i = 0; i < utd->n_type_arguments; ++i) {
            if (utd->type_arguments[i] && utd->type_arguments[i]->type != null) {
                char detail[256];
                snprintf(detail, sizeof detail, "UTD type argument %u has non-null type", i);
                report_invariant_failure(ctx->self, ctx->phase, "Type argument types must be null", detail,
                                         utd->type_arguments[i]);
                ctx->failures++;
            }
        }
    }
}

void check_type_arg_types_null_cb(void *ctx_ptr, ast_node *node) {
    check_type_arg_types_null_one(ctx_ptr, node);
}

int check_type_arg_types_null(tl_infer *self, ast_node_sized nodes, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    forall(i, nodes) {
        ast_node_dfs(&ctx, nodes.v[i], check_type_arg_types_null_cb);
    }
    return ctx.failures;
}

int check_no_generic_toplevels(tl_infer *self, char const *phase) {
    int              failures = 0;
    hashmap_iterator iter     = {0};
    ast_node        *node;
    while ((node = toplevel_iter(self, &iter))) {
        if (!ast_node_is_let(node)) continue;
        if (ast_node_is_specialized(node)) continue; // specialized is OK

        // Check if this function has type parameters (generic)
        struct ast_let *let = &node->let;
        if (let->n_type_parameters > 0) {
            // Generic function - check that it has been fully specialized
            str          name = ast_node_str(let->name);
            tl_polytype *poly = tl_type_env_lookup(self->env, name);
            if (poly && !tl_polytype_is_concrete(poly)) {
                str  tmp = tl_polytype_to_string(self->transient, poly);
                char detail[512];
                snprintf(detail, sizeof detail, "Generic function '%s' still has type variables: %s",
                         str_cstr(&name), str_cstr(&tmp));
                report_invariant_failure(self, phase, "No generic functions should remain", detail, node);
                failures++;
            }
        }
    }
    return failures;
}

// Check that specialized NFA type arguments have concrete types
void check_specialized_nfa_type_args_cb(void *ctx_ptr, ast_node *node) {
    struct check_types_null_ctx *ctx = ctx_ptr;
    if (!ast_node_is_nfa(node)) return;
    if (!node->named_application.is_specialized) return;

    for (u8 i = 0; i < node->named_application.n_type_arguments; i++) {
        ast_node *ta = node->named_application.type_arguments[i];
        if (ta && ta->type && !tl_polytype_is_concrete(ta->type)) {
            char detail[256];
            str  type_str = tl_polytype_to_string(ctx->self->transient, ta->type);
            snprintf(detail, sizeof detail, "Type argument %u has non-concrete type: %s", i,
                     str_cstr(&type_str));
            report_invariant_failure(ctx->self, ctx->phase,
                                     "Specialized NFA type arguments must be concrete", detail, ta);
            ctx->failures++;
        }
    }
}

int check_specialized_nfa_type_args(tl_infer *self, ast_node *node, char const *phase) {
    struct check_types_null_ctx ctx = {.self = self, .phase = phase, .failures = 0};
    ast_node_dfs(&ctx, node, check_specialized_nfa_type_args_cb);
    return ctx.failures;
}

#endif // DEBUG_INVARIANTS
