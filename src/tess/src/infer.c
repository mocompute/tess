// infer.c — Type inference orchestration and shared utilities.
//
// Public API:   tl_infer_create, tl_infer_run, tl_infer_destroy, etc.
// Orchestration: run_alpha_conversion … run_update_types, called by tl_infer_run.
// Utilities:    toplevel_{add,del,get,iter}, name classification, debug logging,
//               apply_subs_to_ast, next_variable_name, next_instantiation.
//
// The heavy lifting is split across:
//   infer_alpha.c       — Phase 1: alpha conversion
//   infer_constraint.c  — Phases 2-4: loading, inference, constraints
//   infer_specialize.c  — Phase 5: monomorphization
//   infer_update.c      — Phases 6-7: tree shaking, type updates

#include "infer_internal.h"

static inline void transient_reset(tl_infer *self) {
    arena_reset(self->transient);
    tl_type_transient_reset();
}

// ============================================================================
// Public API
// ============================================================================

// Build a synthetic arrow AST for a built-in trait signature.
// All parameters are named "T" (the trait's implicit type parameter).
// ret_type is the return type name: "T" for Self, or a concrete name like "CSize".
static ast_node *make_builtin_trait_arrow(allocator *a, u8 arity, char const *ret_type) {
    ast_node_array params = {.alloc = a};
    for (u8 j = 0; j < arity; j++) {
        ast_node *p = ast_node_create_sym_c(a, "T");
        array_push(params, p);
    }
    ast_node *tuple = ast_node_create_tuple(a, (ast_node_sized)array_sized(params));
    ast_node *ret   = ast_node_create_sym_c(a, ret_type);
    return ast_node_create_arrow(a, tuple, ret, (ast_node_sized){0});
}

tl_infer *tl_infer_create(allocator *alloc, tl_infer_opts const *opts) {
    tl_infer *self                  = new(alloc, tl_infer);

    self->opts                      = *opts;

    self->transient                 = arena_create(alloc, 4096);
    self->arena                     = arena_create(alloc, 16 * 1024);
    self->env                       = tl_type_env_create(self->arena);
    self->subs                      = tl_type_subs_create(self->arena);
    self->registry                  = tl_type_registry_create(self->arena, self->transient, self->subs);

    self->synthesized_nodes         = (ast_node_array){.alloc = self->arena};

    self->toplevels                 = null;
    self->traits                    = map_new(self->arena, str, tl_trait_def *, 64);
    self->instances                 = map_new(self->arena, name_and_type, str, 4096);
    self->instance_names            = hset_create(self->arena, 4096);
    self->attributes                = map_new(self->arena, str, void *, 4096);
    self->hash_includes             = (str_array){.alloc = self->arena};
    self->errors                    = (tl_infer_error_array){.alloc = self->arena};

    self->next_var_name             = 0;
    self->next_instantiation        = 0;

    self->verbose                   = 0;
    self->verbose_ast               = 0;
    self->indent_level              = 0;
    self->is_constrain_ignore_error = 0;

    self->report_stats              = 0;
    alloc_zero(&self->phase_stats);
    alloc_zero(&self->counters);

    tl_type_registry_parse_type_ctx_init(self->arena, &self->type_parse_ctx, null);

    tl_type_registry_parse_type_ctx_init(self->arena, &self->hot_parse_ctx, null);
    self->hot_parse_ctx_own_ta = self->hot_parse_ctx.type_arguments;
    self->hot_parse_ctx_guard  = 0;

    self->load_type_arguments  = null;

    // Pre-populate compiler-provided traits
    {
        struct {
            char const *name;
            char const *func;
            u8          arity;
            char const *ret; // "T" = Self, otherwise concrete type name
        } builtins[] = {
          {"Add", "add", 2, "T"},      {"Sub", "sub", 2, "T"},        {"Mul", "mul", 2, "T"},
          {"Div", "div", 2, "T"},      {"Mod", "mod", 2, "T"},        {"BitAnd", "bit_and", 2, "T"},
          {"BitOr", "bit_or", 2, "T"}, {"BitXor", "bit_xor", 2, "T"}, {"Shl", "shl", 2, "T"},
          {"Shr", "shr", 2, "T"},      {"Eq", "eq", 2, "Bool"},       {"Neg", "neg", 1, "T"},
          {"Not", "not", 1, "Bool"},   {"BitNot", "bit_not", 1, "T"},
        };
        for (u32 i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
            tl_trait_def *def = new(self->arena, tl_trait_def);
            def->name         = str_init(self->arena, builtins[i].name);
            def->generic_name = str_init(self->arena, builtins[i].name);
            def->parents      = (str_array){.alloc = self->arena};
            def->sigs         = (tl_trait_sig_array){.alloc = self->arena};
            def->source_node  = null;
            tl_trait_sig sig  = {
               .name  = str_init(self->arena, builtins[i].func),
               .arity = builtins[i].arity,
               .arrow = make_builtin_trait_arrow(self->arena, builtins[i].arity, builtins[i].ret)};
            array_push(def->sigs, sig);
            str_map_set_ptr(&self->traits, def->name, def);
        }
        // Ord inherits from Eq and has cmp
        {
            tl_trait_def *def = new(self->arena, tl_trait_def);
            def->name         = str_init(self->arena, "Ord");
            def->generic_name = str_init(self->arena, "Ord");
            def->parents      = (str_array){.alloc = self->arena};
            def->sigs         = (tl_trait_sig_array){.alloc = self->arena};
            def->source_node  = null;
            str parent        = str_init(self->arena, "Eq");
            array_push(def->parents, parent);
            tl_trait_sig sig = {.name  = str_init(self->arena, "cmp"),
                                .arity = 2,
                                .arrow = make_builtin_trait_arrow(self->arena, 2, "CInt")};
            array_push(def->sigs, sig);
            str_map_set_ptr(&self->traits, def->name, def);
        }
    }

    return self;
}

// Reinit the shared hot_parse_ctx for reuse.  Callers must reinit immediately before each use
// and capture the parse result into a local before calling anything that could re-enter
// (any path through specialize_type_constructor_ or specialize_arrow may call this again).
void hot_parse_ctx_reinit(tl_infer *self, hashmap *outer_type_arguments) {
    if (self->hot_parse_ctx_guard) fatal("reentrancy: hot_parse_ctx reinit while still in use");
    self->hot_parse_ctx_guard = 1;
    map_reset(self->hot_parse_ctx_own_ta);
    tl_type_registry_parse_type_ctx_reinit(
      &self->hot_parse_ctx, outer_type_arguments ? outer_type_arguments : self->hot_parse_ctx_own_ta);
}

// Parse a type argument node, using type_arguments as context for resolving
// type variables (e.g., K -> Int).  Falls back to node->type when fresh parse
// fails (e.g., after type_literal_specialize renamed the node).
// Returns null if neither path produces a result.
tl_monotype *parse_type_arg(tl_infer *self, hashmap *type_arguments, ast_node *node) {
    tl_monotype *parsed;
    if (type_arguments) {
        hot_parse_ctx_reinit(self, type_arguments);
        parsed = tl_type_registry_parse_type_with_ctx(self->registry, node, &self->hot_parse_ctx);
        self->hot_parse_ctx_guard = 0;
    } else {
        parsed = tl_type_registry_parse_type(self->registry, node);
    }
    if (parsed) return parsed;

    if (node->type) return node->type->type;

    return null;
}

void tl_infer_destroy(allocator *alloc, tl_infer **p) {

    if ((*p)->toplevels) map_destroy(&(*p)->toplevels);
    if ((*p)->instances) map_destroy(&(*p)->instances);
    if ((*p)->instance_names) hset_destroy(&(*p)->instance_names);
    if ((*p)->attributes) map_destroy(&(*p)->attributes);

    tl_type_registry_destroy((*p)->registry);
    arena_destroy(&(*p)->transient);
    arena_destroy(&(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void tl_infer_set_verbose(tl_infer *self, int verbose) {
    self->verbose      = verbose;
    self->env->verbose = verbose;
}

void tl_infer_set_verbose_ast(tl_infer *self, int verbose) {
    self->verbose_ast = verbose;
}

void tl_infer_set_attributes(tl_infer *self, ast_node const *sym) {
    if (!ast_node_is_symbol(sym)) return;

    str name = ast_node_str(sym);
    str_map_set_ptr(&self->attributes, name, sym->symbol.attributes);
}

tl_type_registry *tl_infer_get_registry(tl_infer *self) {
    return self->registry;
}

void tl_infer_get_arena_stats(tl_infer *self, arena_stats *main_out, arena_stats *transient_out) {
    arena_get_stats(self->arena, main_out);
    if (transient_out) arena_get_stats(self->transient, transient_out);
}

void tl_infer_set_report_stats(tl_infer *self, int enable) {
    self->report_stats = enable;
}

tl_infer_phase_stats const *tl_infer_get_phase_stats(tl_infer const *self) {
    return &self->phase_stats;
}

tl_infer_counters const *tl_infer_get_counters(tl_infer const *self) {
    return &self->counters;
}

// ============================================================================
// Variable and instantiation naming
// ============================================================================

str next_variable_name(tl_infer *self, str name) {
    int         has_prefix = (0 == str_cmp_nc(name, "tl_", 3));
    char const *fmt        = has_prefix ? "%.*s_v%u" : "tl_%.*s_v%u";
    return str_fmt(self->arena, fmt, str_ilen(name), str_buf(&name), self->next_var_name++);
}

str next_instantiation(tl_infer *self, str name) {
    return str_fmt(self->arena, "%.*s_%u", str_ilen(name), str_buf(&name), self->next_instantiation++);
}

void cancel_last_instantiation(tl_infer *self) {
    self->next_instantiation--;
}

// ============================================================================
// Phase orchestration
// ============================================================================

static int run_alpha_conversion(tl_infer *self, ast_node_sized nodes) {
    rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
    // rename toplevel let-in symbols and keep them in global lexical scope
    forall(i, nodes) rename_let_in(self, nodes.v[i], &ctx);

    // rename the rest
    ctx = (rename_variables_ctx){.lex = ctx.lex};
    forall(i, nodes) rename_variables(self, nodes.v[i], &ctx, 0);
    transient_reset(self);

#if DEBUG_INVARIANTS
    if (check_all_types_null(self, nodes, "Phase 1: Alpha Conversion")) return 1;
    if (check_type_arg_types_null(self, nodes, "Phase 1: Alpha Conversion")) return 1;
    transient_reset(self);
#endif
    return 0;
}

// Phase 2: Load top-level definitions.
static void check_trait_circular_inheritance(tl_infer *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->traits, &iter)) {
        tl_trait_def *def = *(tl_trait_def **)iter.data;
        if (!def->parents.size) continue;

        // Walk the parent chain with a visited set to detect cycles
        hashmap *visited = hset_create(self->transient, 16);
        str_hset_insert(&visited, def->name);
        int       circular = 0;

        str_array work     = {.alloc = self->transient};
        forall(i, def->parents) {
            str p = def->parents.v[i];
            if (str_eq(p, def->name)) {
                circular = 1;
                break;
            }
            if (!str_hset_contains(visited, p)) {
                str_hset_insert(&visited, p);
                array_push(work, p);
            }
        }
        for (u32 w = 0; !circular && w < work.size; w++) {
            tl_trait_def *parent_def = str_map_get_ptr(self->traits, work.v[w]);
            if (parent_def) {
                forall(j, parent_def->parents) {
                    str pp = parent_def->parents.v[j];
                    if (str_eq(pp, def->name)) {
                        circular = 1;
                        break;
                    }
                    if (!str_hset_contains(visited, pp)) {
                        str_hset_insert(&visited, pp);
                        array_push(work, pp);
                    }
                }
            }
        }

        if (circular) {
            array_push(self->errors, ((tl_infer_error){.tag  = tl_err_trait_circular_inheritance,
                                                       .node = def->source_node}));
        }
    }
}

static int run_load_toplevels(tl_infer *self, ast_node_sized nodes) {
    self->toplevels = ast_node_str_map_create(self->arena, 1024);
    load_toplevel(self, nodes);
    transient_reset(self);
    check_trait_circular_inheritance(self);
    transient_reset(self);
    if (self->errors.size) return 1;

    log_toplevels(self);
    return 0;
}

// Phase 3: Generic function type inference.
static int run_generic_inference(tl_infer *self, ast_node_sized nodes) {
    u32 count = 0;
    forall(i, nodes) {
        if (ast_node_is_hash_command(nodes.v[i])) continue;
        if (ast_node_is_type_alias(nodes.v[i])) continue;
        add_generic(self, nodes.v[i]);
        count++;
    }
    if (self->report_stats) self->counters.toplevels_inferred = count;
    transient_reset(self);

    if (self->errors.size) return 1;

#if DEBUG_INVARIANTS
    {
        int failures = 0;
        forall(i, nodes) {
            ast_node *node = nodes.v[i];
            if (!ast_node_is_let(node)) continue;
            str          name = ast_node_str(node->let.name);
            tl_polytype *poly = tl_type_env_lookup(self->env, name);
            if (!poly) {
                char detail[256];
                snprintf(detail, sizeof detail, "Function '%.*s' not found in type environment",
                         str_ilen(name), str_buf(&name));
                report_invariant_failure(self, "Phase 3: Generic Inference",
                                         "All functions must have polytypes in env", detail, node);
                failures++;
            }
        }
        if (failures) return 1;
        transient_reset(self);
    }
#endif
    return 0;
}

// Phase 4: Check free variables and apply substitutions.
static int run_check_free_variables(tl_infer *self) {
    if (check_missing_free_variables(self)) return 1;
    if (self->errors.size) return 1;
    transient_reset(self);

    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    transient_reset(self);

    dbg_at(1, self, "-- inference complete --");
    dbg_at(1, self, "");
    log_toplevels(self);
    log_subs(self);
    log_env(self);
    transient_reset(self);
    return 0;
}

// Phase 5: Generic function specialization.
static int run_specialize(tl_infer *self, ast_node_sized nodes, ast_node *main) {
    dbg_at(1, self, "-- specialize phase");

    // Rewrite binary/unary operators on user-defined types to function calls.
    // Types are concrete after Phase 4, so we can identify user-defined operands.
    rewrite_operator_overloads_all(self);

    // Default weak ints from inference (Phase 3) before specialization begins.
    // This ensures instance cache keys use concrete types (e.g. CLongLong) rather
    // than weak int placeholders, preventing duplicate specializations of closures.
    // A second defaulting pass runs after specialization (below) to handle new
    // weak ints created during post_specialize re-inference.
    tl_type_subs_default_weak_ints(self->subs, tl_type_registry_int(self->registry),
                                   tl_type_registry_uint(self->registry),
                                   tl_type_registry_float(self->registry));

    traverse_ctx *traverse = traverse_ctx_create(self->transient);

    if (main) {
        if (self->report_stats) self->counters.traverse_specialize_calls++;
        traverse_ast(self, traverse, main, specialize_applications_cb);
    } else {
        assert(self->opts.is_library);
        ast_node     *node         = null;
        traverse_ctx *traverse_ctx = traverse_ctx_create(self->transient);
        forall(i, nodes) {
            node = nodes.v[i];

            if (ast_node_is_let(node)) {
                ast_node *name     = toplevel_name_node(node);
                str       fun_name = ast_node_str(name);
                if (is_main_function(fun_name)) continue;
                tl_polytype *type = tl_type_env_lookup(self->env, fun_name);
                if (tl_polytype_is_concrete(type)) {
                    dbg_at(2, self, "library: exporting '%s'", str_cstr(&fun_name));

                    // The type is already concrete, so use it directly for
                    // specialization. Calling make_arrow_with would create fresh
                    // type variables via ensure_tv; for zero-arg functions those
                    // never get unified back, but the reconstruction is
                    // unnecessary at any arity since the type is already known.
                    str inst_name =
                      specialize_arrow(self, traverse_ctx, fun_name, type->type, (tl_monotype_sized){0});
                    // FIXME: ignores specialize_arrow error
                    dbg_at(2, self, "library: exporting '%s' => '%s'", str_cstr(&fun_name),
                           str_cstr(&inst_name));
                }
            }
        }
    }

    // specialize toplevel nodes e.g. global values that may refer to functions by name
    forall(i, nodes) {
        ast_node *node = nodes.v[i];

        if (ast_node_is_let_in(node)) {
            if (self->report_stats) self->counters.traverse_specialize_calls++;
            traverse_ast(self, traverse, node, specialize_applications_cb);
        }
    }

    // specialize module init functions
    {
        // () -> Void
        tl_polytype *callsite = make_arrow_result_type(self, traverse, (ast_node_sized){0},
                                                       tl_polytype_nil(self->arena, self->registry), 0);
        forall(i, nodes) {
            ast_node *node = nodes.v[i];
            if (ast_node_is_let(node)) {
                str name = ast_node_str(node->let.name);
                if (is_module_init(name)) {
                    ast_node_set_is_specialized(node);
                    tl_type_env_insert(self->env, name, callsite);
                    tl_infer_set_attributes(self, node->let.name);
                    post_specialize(self, traverse, node, callsite->type);
                }
            }
        }
    }

    transient_reset(self);

    // Default unconstrained weak integer literals: weak_int_signed -> Int, weak_int_unsigned -> UInt.
    // Must happen after specialization (which re-infers literals, creating new weak types)
    // and before the final substitution pass.
    tl_type_subs_default_weak_ints(self->subs, tl_type_registry_int(self->registry),
                                   tl_type_registry_uint(self->registry),
                                   tl_type_registry_float(self->registry));

    // apply subs to global environment
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    transient_reset(self);

    // ensure main function has the correct type
    if (main) {
        if (check_main_function(self, main)) return 1;
        transient_reset(self);
    }

    remove_generic_toplevels(self);
    if (self->report_stats) self->counters.toplevels_after_specialize = (u32)map_size(self->toplevels);
    transient_reset(self);

#if DEBUG_INVARIANTS
    if (check_no_generic_toplevels(self, "Phase 5: Specialization")) return 1;
    transient_reset(self);
#endif
    return 0;
}

// Phase 6: Tree shaking.
static int run_tree_shake(tl_infer *self, ast_node *main) {
    if (!main) return 0;

    tree_shake_toplevels(self, main);
    if (self->report_stats) self->counters.toplevels_after_tree_shake = (u32)map_size(self->toplevels);
    transient_reset(self);

    // after tree shake, extraneous symbols will have been removed from environment
    if (check_missing_free_variables(self)) return 1;
    if (self->errors.size) return 1;
    return 0;
}

// Phase 7: Type specialization updates.
static int run_update_types(tl_infer *self) {
    update_specialized_types(self);
    transient_reset(self);

    check_unresolved_types(self);
    transient_reset(self);

    check_closure_checks(self);
    transient_reset(self);

    log_subs(self);
    dbg_at(1, self, "-- final env --");
    log_env(self);
    transient_reset(self);
    log_toplevels(self);
    transient_reset(self);

    return self->errors.size ? 1 : 0;
}

// ============================================================================
// Main entry point
// ============================================================================

int tl_infer_run(tl_infer *self, ast_node_sized nodes, tl_infer_result *out_result) {
    dbg_at(1, self, "-- start inference --");

    hires_timer phase_timer;
    if (self->report_stats) hires_timer_init(&phase_timer);

#define PHASE_START()                                                                                      \
    do {                                                                                                   \
        if (self->report_stats) hires_timer_start(&phase_timer);                                           \
    } while (0)
#define PHASE_STOP(field)                                                                                  \
    do {                                                                                                   \
        if (self->report_stats) {                                                                          \
            hires_timer_stop(&phase_timer);                                                                \
            self->phase_stats.field = hires_timer_elapsed_sec(&phase_timer) * 1000.0;                      \
        }                                                                                                  \
    } while (0)

    PHASE_START();
    if (run_alpha_conversion(self, nodes)) return 1;
    PHASE_STOP(alpha_ms);

    PHASE_START();
    if (run_load_toplevels(self, nodes)) return 1;
    PHASE_STOP(load_toplevels_ms);

    PHASE_START();
    if (run_generic_inference(self, nodes)) return 1;
    PHASE_STOP(generic_inference_ms);

    PHASE_START();
    if (run_check_free_variables(self)) return 1;
    PHASE_STOP(free_vars_ms);

    // Default unconstrained weak integer literals before specialization, so that
    // specialization sees concrete Int/UInt types for naming and instance creation.
    tl_type_subs_default_weak_ints(self->subs, tl_type_registry_int(self->registry),
                                   tl_type_registry_uint(self->registry),
                                   tl_type_registry_float(self->registry));
    tl_type_subs_apply(self->subs, self->env);
    apply_subs_to_ast(self);
    transient_reset(self);

    ast_node *main = null;
    if (!self->opts.is_library) {
        ast_node **found_main = str_map_get(self->toplevels, S("main"));
        if (!found_main) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_no_main_function}));
            return 1;
        }
        main = *found_main;
    }

    PHASE_START();
    if (run_specialize(self, nodes, main)) return 1;
    PHASE_STOP(specialize_ms);

    PHASE_START();
    if (run_tree_shake(self, main)) return 1;
    PHASE_STOP(tree_shake_ms);

    PHASE_START();
    if (run_update_types(self)) return 1;
    PHASE_STOP(update_types_ms);

#undef PHASE_START
#undef PHASE_STOP

    if (out_result) {
        out_result->infer     = self;
        out_result->registry  = self->registry;
        out_result->env       = self->env;
        out_result->subs      = self->subs;
        out_result->toplevels = self->toplevels;
        out_result->nodes     = nodes;

        array_shrink(self->synthesized_nodes);
        array_shrink(self->hash_includes);
        out_result->synthesized_nodes = (ast_node_sized)sized_all(self->synthesized_nodes);
        out_result->hash_includes     = (str_sized)sized_all(self->hash_includes);
    }

#if DEBUG_INSTANCE_CACHE
    fprintf(stderr, "\n[INSTANCE CACHE SUMMARY]\n");
    fprintf(stderr, "  Total specializations: %zu\n", map_size(self->instances));
    fprintf(stderr, "  Unique instance names: %zu\n", hset_size(self->instance_names));
#endif

    transient_reset(self);
    return 0;
}

// ============================================================================
// Error reporting and debug logging
// ============================================================================

static void report_error_hints(tl_infer *self, tl_infer_error *err) {
    if (err->tag == tl_err_undeclared_reassignment) {
        fprintf(stderr, "  hint: to declare a new variable, use ':=' instead of '=': %s := ...\n",
                str_cstr(&err->message));
        return;
    }
    if (err->tag == tl_err_free_variable_not_found) {
        // If the free variable name matches a trait method, suggest parenthesizing
        // number literals (e.g., 42.hash() should be (42).hash())
        hashmap_iterator iter = {0};
        while (map_iter(self->traits, &iter)) {
            tl_trait_def *def = *(tl_trait_def **)iter.data;
            forall(s, def->sigs) {
                if (str_eq(def->sigs.v[s].name, err->message)) {
                    fprintf(stderr,
                            "  hint: '%s' is a trait method (trait '%s'). "
                            "If calling on a number literal, parenthesize it: (42).%s()\n",
                            str_cstr(&err->message), str_cstr(&def->generic_name), str_cstr(&err->message));
                    return;
                }
            }
        }
    }
}

void tl_infer_report_errors(tl_infer *self) {
    if (self->errors.size) {
        forall(i, self->errors) {
            tl_infer_error *err     = &self->errors.v[i];
            ast_node const *node    = err->node;
            str             message = str_is_empty(err->message) ? str_empty() : err->message;

            if (node) {
                char const *tag = tl_error_tag_to_user_string(err->tag);

                // Only include AST dump in verbose mode
                int show_ast = self->verbose >= 1 && err->tag != tl_err_free_variable_not_found;
                str node_str = show_ast ? v2_ast_node_to_string(self->transient, node) : str_empty();

                if (node->file && *node->file) {
                    // Print file:line:col when col is known, file:line otherwise
                    if (node->col)
                        fprintf(stderr, "%s:%u:%u: ", node->file, node->line, node->col);
                    else
                        fprintf(stderr, "%s:%u: ", node->file, node->line);

                    if (show_ast)
                        fprintf(stderr, "%s: %s: %s\n", tag, str_cstr(&message), str_cstr(&node_str));
                    else if (!str_is_empty(message))
                        fprintf(stderr, "%s: %s\n", tag, str_cstr(&message));
                    else
                        fprintf(stderr, "%s\n", tag);
                } else {
                    if (show_ast)
                        fprintf(stderr, "%s: %s: %s\n", tag, str_cstr(&message), str_cstr(&node_str));
                    else if (!str_is_empty(message))
                        fprintf(stderr, "%s: %s\n", tag, str_cstr(&message));
                    else
                        fprintf(stderr, "%s\n", tag);
                }
                report_error_hints(self, err);
            } else {
                fprintf(stderr, "error: %s: %s\n", tl_error_tag_to_user_string(err->tag), str_cstr(&message));
            }
        }
    }
}

void tl_infer_dbg(tl_infer const *self, char const *restrict fmt, ...) {
    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "[infer] %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}

static void log_str(tl_infer const *self, str str) {
    if (self->verbose < 3) return;
    tl_infer_dbg(self, "%.*s", str_ilen(str), str_buf(&str));
}

void log_toplevels(tl_infer const *self) {
    if (self->verbose < 3) return;
    dbg_at(3, self, "-- toplevels --");
    str_array sorted = str_map_sorted_keys(self->transient, self->toplevels);
    forall(i, sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, sorted.v[i]);
        str       str;
        if (self->verbose_ast) str = v2_ast_node_to_string(self->transient, node);
        else str = ast_node_to_short_string(self->transient, node);
        log_str(self, str);
        str_deinit(self->transient, &str);
    }
}

void log_env(tl_infer const *self) {
    if (self->verbose >= 3) {
        dbg_at(3, self, "-- env --");
        tl_type_env_log(self->env);
    }
}

void do_apply_subs(void *ctx, ast_node *node) {
    tl_infer *self = ctx;
    if (self->report_stats) self->counters.subs_nodes_visited++;
    if (node->type) {
        tl_polytype_substitute(self->arena, node->type, self->subs);
    }
}

void apply_subs_to_ast(tl_infer *self) {
    if (self->report_stats) self->counters.subs_apply_calls++;
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        ast_node_dfs(self, node, do_apply_subs);
    }
}

// ============================================================================
// Name classification helpers and toplevel map accessors
// ============================================================================

int is_intrinsic(str name) {
    return (0 == str_cmp_nc(name, "_tl_", 4));
}

int is_c_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_", 2));
}

int is_c_struct_symbol(str name) {
    return (0 == str_cmp_nc(name, "c_struct_", 9));
}

int is_module_init(str name) {
    return str_ends_with(name, S("____init__0"));
}

int is_main_function(str name) {
    return str_eq(name, S("main"));
}

//

void toplevel_add(tl_infer *self, str name, ast_node *node) {
    ast_node_str_map_add(&self->toplevels, name, node);
}

void toplevel_del(tl_infer *self, str name) {
    ast_node_str_map_erase(self->toplevels, name);
}

ast_node *toplevel_get(tl_infer *self, str name) {
    return ast_node_str_map_get(self->toplevels, name);
}

ast_node *toplevel_iter(tl_infer *self, hashmap_iterator *iter) {
    return ast_node_str_map_iter(self->toplevels, iter);
}

ast_node *toplevel_name_node(ast_node *node) {
    if (ast_node_is_let(node)) return node->let.name;
    else if (ast_node_is_let_in(node)) return node->let_in.name;
    else if (ast_node_is_symbol(node)) return node;
    else if (ast_node_is_trait_def(node)) return node->trait_def.name;
    else if (ast_node_is_utd(node)) return node->user_type_def.name;
    else if (ast_node_is_nfa(node)) return node->named_application.name;
    else if (ast_node_is_type_alias(node)) {
        if (ast_node_is_symbol(node->type_alias.name)) return node->type_alias.name;
        else fatal("runtime error");
    } else fatal("logic error");
}

str toplevel_name(ast_node const *node) {
    return toplevel_name_node((ast_node *)node)->symbol.name;
}

void log_constraint(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    if (self->verbose < 3) return;
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);
    str node_str  = v2_ast_node_to_string(self->transient, node);
    dbg(self, "constrain: %s : %s from %s", str_cstr(&left_str), str_cstr(&right_str), str_cstr(&node_str));
}

void log_type_error(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node) {
    // Note: always print err to stderr
    str left_str  = tl_polytype_to_string(self->transient, left);
    str right_str = tl_polytype_to_string(self->transient, right);

    fprintf(stderr, "%s:%i: error: conflicting types: %s versus %s\n", node->file, node->line,
            str_cstr(&left_str), str_cstr(&right_str));
}
void log_type_error_mm(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node) {
    tl_polytype l = tl_polytype_wrap((tl_monotype *)left), r = tl_polytype_wrap((tl_monotype *)right);
    log_type_error(self, &l, &r, node);
}

void log_subs(tl_infer *self) {
    if (self->verbose >= 3) {
        dbg_at(3, self, "-- subs --");
        tl_type_subs_log(self->subs);
    }
}
