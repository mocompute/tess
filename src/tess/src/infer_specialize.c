// infer_specialize.c — Phase 5: Monomorphization / Specialization
//
// Clones generic functions and type constructors at their concrete call-site
// types, creating specialized (monomorphic) versions.  Also contains arrow type
// construction, the instance cache, generic function registration (Phase 3),
// and post-inference validation helpers.

#include "infer_internal.h"

#include <string.h>

// ============================================================================
// Arrow construction
// ============================================================================

tl_polytype *make_arrow_result_type(tl_infer *self, traverse_ctx *ctx, ast_node_sized args,
                                    tl_polytype *result_type, int is_parameters) {
    if (args.size == 0 || (args.size == 1 && ast_node_is_nil(args.v[0]))) {
        // always use a tuple on the left side of arrow, even if zero elements
        tl_monotype *lhs   = tl_monotype_create_tuple(self->arena, (tl_monotype_sized){0});
        tl_monotype *rhs   = result_type ? result_type->type : null;
        tl_monotype *arrow = tl_type_registry_create_arrow(self->registry, lhs, rhs);

        {
            str str = tl_monotype_to_string(self->transient, arrow);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }
        return tl_polytype_absorb_mono(self->arena, arrow);
    }

    else {
        tl_monotype_array args_types = {.alloc = self->arena};
        array_reserve(args_types, args.size);
        forall(i, args) {
            if (resolve_node(self, args.v[i], ctx,
                             is_parameters ? npos_formal_parameter : npos_function_argument))
                return null;

            tl_monotype *mono = args.v[i]->type->type;

            // make concrete if possible
            tl_monotype_substitute(self->arena, mono, self->subs, null);
            array_push(args_types, mono);
        }

        tl_monotype *left = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(args_types));
        tl_monotype *right = null;
        if (result_type) {
            right = result_type->type;
        } else {
            right = tl_type_registry_nil(self->registry);
        }

        tl_monotype *out = tl_type_registry_create_arrow(self->registry, left, right);

        if (self->verbose >= 3) {
            str str = tl_monotype_to_string(self->transient, out);
            dbg(self, "arrow: %.*s", str_ilen(str), str_buf(&str));
            str_deinit(self->transient, &str);
        }

        return tl_polytype_absorb_mono(self->arena, out);
    }
}

tl_polytype *make_arrow(tl_infer *self, traverse_ctx *ctx, ast_node_sized args, ast_node *result,
                        int is_parameters) {
    if (result) ensure_tv(self, &result->type);
    return make_arrow_result_type(self, ctx, args, result ? result->type : null, is_parameters);
}

tl_polytype *make_arrow_with(tl_infer *self, traverse_ctx *ctx, ast_node *node, tl_polytype *type) {
    ast_arguments_iter iter = ast_node_arguments_iter(node); // not used for iter, just for args
    tl_polytype       *out  = make_arrow(self, ctx, iter.nodes, node, 0);

    if (!out) return null;
    if (tl_monotype_is_list(out->type) && tl_monotype_is_list(type->type)) {
        (out->type)->list.fvs = type->type->list.fvs;
#if DEBUG_RECURSIVE_TYPES
        {
            str out_str = tl_monotype_to_string(self->transient, out->type);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] make_arrow_with: arrow=%s fvs_count=%u\n",
                    str_cstr(&out_str), type->type->list.fvs.size);
            forall(i, type->type->list.fvs) {
                fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   fv[%u] = '%s'\n", i,
                        str_cstr(&type->type->list.fvs.v[i]));
            }
        }
#endif
    }
    return out;
}

tl_polytype *make_binary_predicate_arrow(tl_infer *self, traverse_ctx *ctx, ast_node *lhs, ast_node *rhs) {

    ast_node_array args = {.alloc = self->arena};
    array_push(args, lhs);
    array_push(args, rhs);
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);

    tl_polytype *bool_poly = tl_polytype_absorb_mono(self->arena, bool_type);
    tl_polytype *pred_arrow =
      make_arrow_result_type(self, ctx, (ast_node_sized)array_sized(args), bool_poly, 0);

    return pred_arrow;
}

// ============================================================================
// Generic cloning
// ============================================================================

ast_node *clone_generic_for_arrow(tl_infer *self, ast_node const *node, tl_monotype *arrow, str inst_name,
                                  hashmap *type_arguments, tl_monotype_sized resolved_type_args) {
    ast_node *clone = ast_node_clone(self->arena, node);
    ast_node *name  = toplevel_name_node(clone);
    assert(ast_node_is_symbol(name));

    // rename variables: also erases type information
    rename_variables_ctx ctx     = {.lex = map_new(self->transient, str, str, 16)};

    name->symbol.annotation_type = null;
    name->symbol.annotation      = null;

    rename_variables(self, clone, &ctx, 0);

#if DEBUG_INVARIANTS
    // Invariant: After clone + rename_variables, all types must be erased (null)
    {
        struct check_types_null_ctx check_ctx = {
          .self = self, .phase = "clone_generic_for_arrow", .failures = 0};
        ast_node_dfs(&check_ctx, clone, check_types_null_cb);
        if (check_ctx.failures) {
            fprintf(stderr, "ERROR: Type pollution detected in cloned AST\n");
        }
    }

    // Invariant: Alpha-converted type parameter names must not already exist in the environment
    // This ensures each specialization gets truly fresh names
    if (ast_node_is_let(clone)) {
        for (u32 i = 0; i < clone->let.n_type_parameters; i++) {
            ast_node    *tp       = clone->let.type_parameters[i];
            str          tp_name  = tp->symbol.name;

            tl_polytype *existing = tl_type_env_lookup(self->env, tp_name);
            if (existing) {
                char detail[256];
                str  type_str = tl_polytype_to_string(self->transient, existing);
                snprintf(detail, sizeof detail,
                         "Type parameter '%.*s' already exists in environment with type: %s",
                         str_ilen(tp_name), str_buf(&tp_name), str_cstr(&type_str));
                report_invariant_failure(self, "clone_generic_for_arrow",
                                         "New specialization type parameters must have fresh names", detail,
                                         tp);
            }
        }
    }
#endif

    // recalculate free variables, because symbol names have been renamed
    tl_polytype wrap = tl_polytype_wrap(arrow);
    add_free_variables_to_arrow(self, clone, &wrap);

    concretize_params(self, clone, arrow, type_arguments, resolved_type_args);

#if DEBUG_INVARIANTS
    // Invariant: Type parameters with explicit bindings in type_arguments must have concrete types
    if (ast_node_is_let(clone) && type_arguments) {
        for (u32 i = 0; i < clone->let.n_type_parameters; i++) {
            ast_node *tp      = clone->let.type_parameters[i];
            str       tp_name = tp->symbol.name;

            // Only check type parameters that have an explicit binding in type_arguments
            tl_monotype *bound_type = str_map_get_ptr(type_arguments, tp_name);
            if (!bound_type) continue; // No explicit binding, skip this type parameter

            if (!tp->type || !tl_polytype_is_concrete(tp->type)) {
                char detail[256];
                if (tp->type) {
                    str type_str = tl_polytype_to_string(self->transient, tp->type);
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' with explicit binding has non-concrete type: %s",
                             str_ilen(tp_name), str_buf(&tp_name), str_cstr(&type_str));
                } else {
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' with explicit binding has null type", str_ilen(tp_name),
                             str_buf(&tp_name));
                }
                report_invariant_failure(
                  self, "clone_generic_for_arrow",
                  "Type parameter with explicit binding must have concrete type after concretize_params",
                  detail, tp);
            }
        }
    }
#endif

    toplevel_name_replace(clone, inst_name);

    return clone;
}

// ============================================================================
// Type literal specialization
// ============================================================================

static str specialize_type_constructor_(tl_infer *self, str name, tl_monotype_sized args,
                                        tl_polytype **out_type, hashmap **seen);
str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args, tl_polytype **out_type);

int type_literal_specialize(tl_infer *self, ast_node *node, hashmap *type_arguments) {
    // specialize a type id, e.g. `Point(Int)`. Contrast to specialize_type_constructor, which specialises
    // based on a callsite like `Point(1, 2)`. Assuming Point(a) { x : a, y : a }.
    // return 1 if node is not a type identifier or other error occurs.

    if (ast_node_is_symbol(node)) return 1;

    // Value constructors with named field arguments (e.g. Simple[Int](data = null, size = 0u))
    // are not type literals. They must go through specialize_user_type for proper struct
    // specialization including field type resolution and value argument handling.
    if (ast_node_is_nfa(node) && node->named_application.is_type_constructor) {
        ast_arguments_iter check = ast_node_arguments_iter((ast_node *)node);
        ast_node          *a;
        while ((a = ast_arguments_next(&check))) {
            if (ast_node_is_assignment(a)) return 1;
        }
    }

    // Parse with context when available, so type variables from the outer generic
    // function (e.g. K, V) resolve to their concrete bindings (e.g. Int, Int).
    // Without context, these become fresh unresolvable type variables.
    tl_monotype *parsed = parse_type_arg(self, type_arguments, node);
    if (parsed) {
        tl_monotype *target = parsed;
        if (!tl_monotype_is_inst(target)) return 1;
        str name = target->cons_inst->def->generic_name;

#if DEBUG_RECURSIVE_TYPES
        {
            str parsed_str = tl_monotype_to_string(self->transient, parsed);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: parsed=%s name='%s'\n",
                    str_cstr(&parsed_str), str_cstr(&name));
        }
#endif

        tl_monotype_sized args = target->cons_inst->args;
        tl_monotype      *inst = tl_type_registry_get_cached_specialization(self->registry, name, args);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: cache %s for '%s'\n",
                inst ? "HIT" : "MISS", str_cstr(&name));
#endif
        str          name_inst    = str_empty();
        tl_polytype *special_type = null;
        if (!inst) {
            name_inst = specialize_type_constructor(self, name, args, &special_type);
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] type_literal_specialize: specialize returned '%s' type=%p\n",
                    str_is_empty(name_inst) ? "(empty)" : str_cstr(&name_inst), (void *)special_type);
#endif
            // ok to fail: enums, nullary builtins, etc
            if (str_is_empty(name_inst)) return 0;
            if (!special_type) return 0;
        } else {
            name_inst    = inst->cons_inst->special_name;
            special_type = tl_polytype_absorb_mono(self->arena, inst);
        }

        if (ast_node_is_symbol(node)) {
            ast_node_name_replace(node, name_inst);
            if (node->type) {
                if (constrain(self, node->type, special_type, node, TL_UNIFY_SYMMETRIC)) return 1;
            } else {
                ast_node_type_set(node, special_type);
            }
        } else if (ast_node_is_nfa(node)) {
            ast_node_name_replace(node->named_application.name, name_inst);
            if (node->named_application.name->type) {
                if (constrain(self, node->named_application.name->type, special_type, node,
                              TL_UNIFY_SYMMETRIC))
                    return 1;
            } else {
                ast_node_type_set(node->named_application.name, special_type);
            }

        } else fatal("logic error");

        return 0;
    }

    return 1;
}

int is_type_literal(tl_infer *self, traverse_ctx const *ctx, ast_node const *node) {
    // If the node has any assignment arguments (e.g., Wrapper[Int](v = 1.0)),
    // it's a value constructor call, not a type literal.
    if (ast_node_is_nfa(node)) {
        if (node->named_application.is_type_constructor) return 0;
        ast_arguments_iter iter = ast_node_arguments_iter((ast_node *)node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            if (ast_node_is_assignment(arg)) {
                return 0; // has value arguments, not a type literal
            }
        }
    }

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype *mono = tl_type_registry_parse_type_out_ctx(self->registry, node, self->transient,
                                                            ctx->type_arguments, &parse_ctx);
#if DEBUG_EXPLICIT_TYPE_ARGS
    if (mono && ast_node_is_nfa(node)) {
        str name     = ast_node_str(node->named_application.name);
        str mono_str = tl_monotype_to_string(self->transient, mono);
        fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] is_type_literal: %s parsed as type: %s\n",
                str_cstr(&name), str_cstr(&mono_str));
    }
#endif
    return !!mono;
}

// ============================================================================
// Instance cache
// ============================================================================

name_and_type make_instance_key(tl_infer *self, str generic_name, tl_monotype *arrow,
                                tl_monotype_sized resolved_type_args) {

    forall(i, resolved_type_args) {
        if (!resolved_type_args.v[i]) continue;

        if (!tl_monotype_is_concrete(resolved_type_args.v[i])) {
            // Clone before substituting: parse_type_arg may return node->type->type
            // (a direct pointer into the AST), so in-place substitution would corrupt the AST.
            resolved_type_args.v[i] = tl_monotype_clone(self->arena, resolved_type_args.v[i]);
            tl_monotype_substitute(self->arena, resolved_type_args.v[i], self->subs, null);
        }

        // Keep non-concrete type args in the hash as-is (don't null them out).
        // Different non-concrete types (e.g. Inner[K,V] vs Outer[K,V]) are structurally
        // different and hash differently, so the cache correctly distinguishes them.
    }

    name_and_type key = {
      .name_hash      = str_hash64(generic_name),
      .type_hash      = tl_monotype_hash64(arrow),
      .type_args_hash = tl_monotype_sized_hash64(hash64("args", 4), resolved_type_args),
    };

#if DEBUG_INSTANCE_CACHE
    {
        str arrow_str = tl_monotype_to_string(self->transient, arrow);
        fprintf(stderr, "[INSTANCE_KEY] name='%s' arrow='%s' n_type_args=%u\n", str_cstr(&generic_name),
                str_cstr(&arrow_str), resolved_type_args.size);
        fprintf(stderr, "  -> name_hash=%016llx type_hash=%016llx type_args_hash=%016llx\n",
                (unsigned long long)key.name_hash, (unsigned long long)key.type_hash,
                (unsigned long long)key.type_args_hash);
        forall(i, resolved_type_args) {
            str ta_str = tl_monotype_to_string(self->transient, resolved_type_args.v[i]);
            fprintf(stderr, "  type_arg[%u] = '%s' (hash=%016llx)\n", i, str_cstr(&ta_str),
                    (unsigned long long)tl_monotype_hash64(resolved_type_args.v[i]));
        }
    }
#endif

    return key;
}

str *instance_lookup(tl_infer *self, name_and_type *key) {
    return map_get(self->instances, key, sizeof *key);
}

str *instance_lookup_arrow(tl_infer *self, str generic_name, tl_monotype *arrow,
                           tl_monotype_sized resolved_type_args) {
    if (!tl_monotype_is_concrete(arrow)) return null;

    // de-duplicate instances: hashes give us structural equality (barring hash collisions), which we need
    // because types are frequently cloned.
    name_and_type key    = make_instance_key(self, generic_name, arrow, resolved_type_args);

    str          *result = instance_lookup(self, &key);

#if DEBUG_INSTANCE_CACHE
    if (result) {
        fprintf(stderr, "[CACHE HIT] '%s' -> '%s'\n", str_cstr(&generic_name), str_cstr(result));
    } else {
        fprintf(stderr, "[CACHE MISS] '%s'\n", str_cstr(&generic_name));
    }
#endif

    return result;
}

int instance_name_exists(tl_infer *self, str instance_name) {
    // NB: here, the set is keyed by _instance_ name, not generic name.
    return str_hset_contains(self->instance_names, instance_name);
}

static void instance_add(tl_infer *self, name_and_type *key, str instance_name) {
#if DEBUG_INSTANCE_CACHE
    size_t count_before = map_size(self->instances);
    fprintf(stderr, "[INSTANCE ADD] '%s' (cache size: %zu -> %zu)\n", str_cstr(&instance_name),
            count_before, count_before + 1);
    fprintf(stderr, "  key: name_hash=%016llx type_hash=%016llx type_args_hash=%016llx\n",
            (unsigned long long)key->name_hash, (unsigned long long)key->type_hash,
            (unsigned long long)key->type_args_hash);
#endif
    map_set(&self->instances, key, sizeof *key, &instance_name);
    str_hset_insert(&self->instance_names, instance_name);
}

// ============================================================================
// Type constructor specialization
// ============================================================================

static str specialize_type_constructor_(tl_infer *self, str name, tl_monotype_sized args,
                                        tl_polytype **out_type, hashmap **seen) {
    if (out_type) *out_type = null;

#if DEBUG_RECURSIVE_TYPES
    {
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor_ ENTER: name='%s' n_args=%u\n",
                str_cstr(&name), args.size);
        forall(i, args) {
            str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s  (is_inst=%d, is_inst_spec=%d)\n", i,
                    str_cstr(&arg_str), tl_monotype_is_inst(args.v[i]),
                    tl_monotype_is_inst(args.v[i]) ? tl_monotype_is_inst_specialized(args.v[i]) : 0);
        }
    }
#endif

    // do not specialize if it's an enum
    {
        ast_node *utd = toplevel_get(self, name);
        if (utd && ast_node_is_enum_def(utd)) return str_empty();
    }

    // Normalize type alias names to their canonical generic_name so that auto-collapsed aliases
    // (e.g. "Point" aliasing "Point__T") use the same cache key as the original name.
    if (tl_type_registry_is_type_alias(self->registry, name)) {
        tl_polytype *alias_poly = tl_type_registry_get(self->registry, name);
        if (alias_poly && tl_monotype_is_inst(alias_poly->type)) {
            name = alias_poly->type->cons_inst->def->generic_name;
        }
    }

    {
        name_and_type key = {.name_hash = str_hash64(name), .type_hash = tl_monotype_sized_hash64(0, args)};
        if (hset_contains(*seen, &key, sizeof key)) {
#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES]   CYCLE DETECTED in 'seen' for name='%s' "
                    "(name_hash=%016llx, type_hash=%016llx)\n",
                    str_cstr(&name), (unsigned long long)key.name_hash, (unsigned long long)key.type_hash);
#endif
            return str_empty();
        }
        hset_insert(seen, &key, sizeof key);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   added to 'seen': name='%s'\n", str_cstr(&name));
#endif
    }

    // To keep track of monotypes that are recursive references to the type being specialized.
    tl_monotype_ptr_array recur_refs = {.alloc = self->transient};

    // specialize args first
    forall(i, args) {
        if (tl_monotype_is_inst(args.v[i]) && !tl_monotype_is_inst_specialized(args.v[i])) {
            tl_polytype *poly         = null;
            tl_monotype *arg_mono     = args.v[i];
            str          generic_name = arg_mono->cons_inst->def->generic_name;

            // Do not recurse: fixup after
            if (str_eq(name, generic_name)) {
#if DEBUG_RECURSIVE_TYPES
                fprintf(stderr,
                        "[DEBUG_RECURSIVE_TYPES]   DIRECT SELF-REF: arg[%u] '%s' matches name='%s'\n", i,
                        str_cstr(&generic_name), str_cstr(&name));
#endif
                {
                    tl_monotype **_t = &args.v[i];
                    array_push(recur_refs, _t);
                }
                continue;
            }

            // Do not recurse into pointer target: fixup after
            if (tl_monotype_is_ptr(args.v[i])) {
                tl_monotype *target = tl_monotype_ptr_target(args.v[i]);
                if (tl_monotype_is_inst(target) && str_eq(name, target->cons_inst->def->generic_name)) {
#if DEBUG_RECURSIVE_TYPES
                    fprintf(
                      stderr,
                      "[DEBUG_RECURSIVE_TYPES]   PTR SELF-REF: arg[%u] Ptr to '%s' matches name='%s'\n", i,
                      str_cstr(&generic_name), str_cstr(&name));
#endif
                    {
                        tl_monotype **_t = &arg_mono->cons_inst->args.v[0];
                        array_push(recur_refs, _t);
                    }
                    continue;
                }
            }

#if DEBUG_RECURSIVE_TYPES
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES]   RECURSE for arg[%u]: generic_name='%s' (parent='%s')\n", i,
                    str_cstr(&generic_name), str_cstr(&name));
#endif
            (void)specialize_type_constructor_(self, generic_name, args.v[i]->cons_inst->args, &poly, seen);
#if DEBUG_RECURSIVE_TYPES
            {
                str poly_str =
                  poly ? tl_polytype_to_string(self->transient, poly) : str_init(self->transient, "(null)");
                fprintf(
                  stderr, "[DEBUG_RECURSIVE_TYPES]   RECURSE result arg[%u] '%s': poly=%s concrete=%d\n", i,
                  str_cstr(&generic_name), str_cstr(&poly_str), poly ? tl_polytype_is_concrete(poly) : 0);
            }
#endif
            if (poly && tl_polytype_is_concrete(poly)) {
                args.v[i] = tl_polytype_concrete(poly);
            }
        }
    }

    str                             out_str   = str_empty();
    str                             name_inst = next_instantiation(self, name); // may be cancelled later
    tl_type_registry_specialize_ctx inst_ctx =
      tl_type_registry_specialize_begin(self->registry, name, name_inst, args);

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   registry_begin: name='%s' name_inst='%s' specialized=%p\n",
            str_cstr(&name), str_cstr(&name_inst), (void *)inst_ctx.specialized);
    if (inst_ctx.specialized) {
        str spec_str = tl_monotype_to_string(self->transient, inst_ctx.specialized);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   specialized type = %s\n", str_cstr(&spec_str));
    }
#endif

#if DEBUG_EXPLICIT_TYPE_ARGS
    fprintf(stderr, "[DEBUG specialize_type_constructor_] name=%s\n", str_cstr(&name));
    fprintf(stderr, "  inst_ctx.specialized = %p\n", (void *)inst_ctx.specialized);
#endif

    if (!inst_ctx.specialized) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: inst_ctx.specialized is null\n");
#endif
        goto cancel;
    }
    if (!tl_monotype_is_inst(inst_ctx.specialized)) fatal("runtime error");

    name_and_type key      = make_instance_key(self, name, inst_ctx.specialized, (tl_monotype_sized){0});
    str          *existing = instance_lookup(self, &key);
    if (existing) {
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   EXISTING instance: '%s' -> '%s'\n", str_cstr(&name),
                str_cstr(existing));
#endif
        tl_polytype *poly = tl_type_env_lookup(self->env, *existing);
        if (out_type) *out_type = poly;
        out_str = *existing;

#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: existing instance found: %s\n", str_cstr(existing));
#endif
        goto cancel;
    }

    // Look up generic type using the generic_name field, not the name parameter, because the latter may be
    // a type alias.
    ast_node *utd = toplevel_get(self, inst_ctx.specialized->cons_inst->def->generic_name);
#if DEBUG_EXPLICIT_TYPE_ARGS
    {
        str gn = inst_ctx.specialized->cons_inst->def->generic_name;
        fprintf(stderr, "  generic_name for toplevel_get: '%s' (len=%zu, hash=%llu)\n", str_cstr(&gn),
                str_len(gn), (unsigned long long)str_hash64(gn));
        fprintf(stderr, "  utd = %p, toplevels = %p\n", (void *)utd, (void *)self->toplevels);
    }
#endif
    if (!utd) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "  -> cancel: utd not found\n");
#endif
        goto cancel;
    }

    instance_add(self, &key, name_inst);

    utd = ast_node_clone(self->arena, utd);
    ast_node_name_replace(utd->user_type_def.name, name_inst);
    utd->type = tl_polytype_absorb_mono(self->arena, inst_ctx.specialized);
    toplevel_add(self, name_inst, utd);
    tl_type_env_insert(self->env, name_inst, utd->type);
    tl_infer_set_attributes(self, utd->user_type_def.name);
    array_push(self->synthesized_nodes, utd);

    assert(tl_monotype_is_inst_specialized(utd->type->type));
    tl_polytype *save_type = utd->type;
    if (out_type) *out_type = utd->type; // Note: this helps the transpiler

#if DEBUG_EXPLICIT_TYPE_ARGS
    fprintf(stderr, "[DEBUG specialize] Added synthesized node: %s\n", str_cstr(&name_inst));
#endif

    // fixup recur refs
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   FIXUP: %u recur_refs for '%s'\n", recur_refs.size,
            str_cstr(&name));
    if (recur_refs.size) {
        str fixup_str = tl_monotype_to_string(self->transient, utd->type->type);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   fixup target = %s\n", str_cstr(&fixup_str));
    }
#endif
    forall(i, recur_refs) {
        *recur_refs.v[i] = utd->type->type;
    }
    array_free(recur_refs);

    tl_type_registry_specialize_commit(self->registry, inst_ctx);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   COMMIT: '%s' -> '%s'\n", str_cstr(&name),
            str_cstr(&name_inst));
#endif

    // rename variables: also erases type information
    {
        rename_variables_ctx ctx = {.lex = map_new(self->transient, str, str, 16)};
        rename_variables(self, utd, &ctx, 0);

        // restore type, for the transpiler
        utd->type = save_type;
    }

    return name_inst;

cancel:
    cancel_last_instantiation(self);
    return out_str;
}

str specialize_type_constructor(tl_infer *self, str name, tl_monotype_sized args, tl_polytype **out_type) {

#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor ENTRY: name='%s' n_args=%u\n",
            str_cstr(&name), args.size);
    forall(i, args) {
        str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s\n", i, str_cstr(&arg_str));
    }
#endif
    hashmap *seen = hset_create(self->transient, 64);
    str      out  = specialize_type_constructor_(self, name, args, out_type, &seen);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_type_constructor RESULT: name='%s' => '%s'\n",
            str_cstr(&name), str_is_empty(out) ? "(empty)" : str_cstr(&out));
#endif
    return out;
}

int is_union_struct(tl_infer *self, str name) {
    ast_node *utd = toplevel_get(self, name);
    if (utd && ast_node_is_union_def(utd)) return 1;
    return 0;
}

// ============================================================================
// Specialization helpers
// ============================================================================

static int specialize_value_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                      tl_monotype_sized expected_types);

static int specialize_user_type(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // divert if type constructor application is actually a type literal
    if (0 == type_literal_specialize(self, node, traverse_ctx ? traverse_ctx->type_arguments : null))
        return 0;

    if (!ast_node_is_nfa(node)) return 0;

    str name = node->named_application.name->symbol.name;
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type ENTRY: name='%s'\n", str_cstr(&name));
#endif

    tl_monotype_array arr       = {.alloc = self->transient};
    tl_monotype_sized arr_sized = {0};

    // Check if type being constructed is concrete. If so, we want to take its arguments' concrete types
    // rather than instantiate into new type variables.
    tl_polytype *existing = tl_type_registry_get(self->registry, name);
    if (existing && tl_polytype_is_concrete(existing)) {
        assert(tl_monotype_is_inst(existing->type));

        arr_sized = existing->type->cons_inst->args;
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr,
                "[DEBUG_RECURSIVE_TYPES] specialize_user_type: concrete existing for '%s' n_args=%u\n",
                str_cstr(&name), arr_sized.size);
#endif

        // If name is a type alias pointing to a concrete type, we want the transpiler to ignore the alias
        // name, and act as if the alias' target was referenced directly. This ensures the same type is used
        // in the generated C code, allowing variables to be assignable.
        if (tl_type_registry_is_type_alias(self->registry, name)) {
            name = existing->type->cons_inst->def->generic_name;
        }

    } else if (is_union_struct(self, name)) {
        // For unions, get args from the node's inferred type, not AST arguments.
        // Union constructions pass only one variant value, but specialization
        // needs all variant types from the inferred type.
        if (node->type && tl_monotype_is_inst(node->type->type)) {
            tl_monotype *mono = node->type->type;
            arr_sized         = mono->cons_inst->args;
        } else {
            return 0; // Type not ready yet
        }
    } else {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {

            // Note: parse_type may return an integer type (i64) when ast is an integer literal
            tl_monotype *type_id = null;
            if ((type_id = tl_type_registry_parse_type_except_integer_literal(self->registry, arg))) {
                // a literal type
                {
                    fatal("oops: a type literal?");
                    array_push(arr, type_id);
                }
                continue;
            }

            // For struct field assignments with a cast annotation (Ptr or integer),
            // use the annotation type instead of the value type for specialization.
            tl_polytype *arg_type = arg->type;
            if (ast_node_is_assignment(arg) && is_cast_annotation(arg->assignment.name)) {
                arg_type = arg->assignment.name->symbol.annotation_type;
            }

            tl_monotype *mono = null;
            if (!tl_polytype_is_concrete(arg_type)) {
                mono = tl_polytype_instantiate(self->arena, arg_type, self->subs);
                tl_monotype_substitute(self->arena, mono, self->subs, null); // needed
            } else {
                mono = arg_type->type;
            }

            array_push(arr, mono);
        }

        assert(arr.size == node->named_application.n_arguments);
        arr_sized = (tl_monotype_sized)array_sized(arr);
#if DEBUG_RECURSIVE_TYPES
        fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type: iterated args for '%s' n_args=%u\n",
                str_cstr(&name), arr_sized.size);
        forall(j, arr_sized) {
            str arg_str = tl_monotype_to_string(self->transient, arr_sized.v[j]);
            fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   arg[%u] = %s (concrete=%d)\n", j, str_cstr(&arg_str),
                    tl_monotype_is_concrete(arr_sized.v[j]));
        }
#endif
    }

    tl_polytype *special_type = null;
    str          name_inst    = specialize_type_constructor(self, name, arr_sized, &special_type);
#if DEBUG_RECURSIVE_TYPES
    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES] specialize_user_type: result for '%s' => '%s' type=%p\n",
            str_cstr(&name), str_is_empty(name_inst) ? "(empty)" : str_cstr(&name_inst),
            (void *)special_type);
#endif
    if (str_is_empty(name_inst)) return 0;

    // update callsite
    ast_node_name_replace(node->named_application.name, name_inst);
    ast_node_set_is_specialized(node);
    if (special_type) {

        assert(tl_monotype_is_inst_specialized(special_type->type));

        // Note: For type constructors being specialized, we must always override the node type.
        ast_node_type_set(node, special_type);
    }

    // Specialize function pointer arguments in struct initialization.
    // This must be done for both generic and non-generic type constructors,
    // as function pointer arguments need to reference specialized function names.
    if (node->named_application.n_arguments > 0) {
        if (specialize_value_arguments(self, traverse_ctx, node, arr_sized)) {
            return 1;
        }
    }

    return 0;
}

ast_node *get_infer_target(ast_node *node) {
    if (ast_node_is_let(node) || ast_node_is_lambda_function(node)) {
        return node;
    }

    else if (ast_node_is_let_in(node)) {
        return node->let_in.value;
    }

    else if (ast_node_is_symbol(node)) {
        return null;
    }

    return null;
}

int  specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node);

void toplevel_name_replace(ast_node *node, str name_replace) {
    if (ast_node_is_let(node)) {
        ast_node_name_replace(node->let.name, name_replace);
        ast_node_set_is_specialized(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        ast_node_name_replace(node->let_in.name, name_replace);
    } else if (ast_node_is_symbol(node)) {
        // no body
        ;
    } else {
        fatal("logic error");
    }
}

void specialized_add_to_env(tl_infer *self, str inst_name, tl_monotype *mono) {
    // add to type environment
    if (!tl_monotype_is_concrete(mono)) {
        // Note: functions like c_malloc etc will not have concrete types but still need to exist in the
        // environment.
        str arrow_str = tl_monotype_to_string(self->transient, mono);
        dbg_at(2, self, "note: adding non-concrete type to environment: '%s' : %s", str_cstr(&inst_name),
               str_cstr(&arrow_str));
    }
    tl_type_env_insert_mono(self->env, inst_name, mono);
}

void apply_subs_to_ast_node(tl_infer *self, ast_node *node) {
    if (self->report_stats) self->counters.subs_apply_calls++;
    ast_node_dfs(self, node, do_apply_subs);
}

// Forward declaration — defined in trait bound section below.
static int has_no_conform(tl_infer *self, tl_monotype *concrete_type, char const *trait_generic_name);

// ============================================================================
// Operator overloading — rewrite binary/unary ops on user-defined types to NFAs
// ============================================================================

// Map binary operator string to the trait function name. Returns null if not overloadable.
// Note: keep in sync with func_name_to_trait_name() below.
static char const *binary_op_to_func_name(char const *op) {
    if (0 == strcmp(op, "+")) return "add";
    if (0 == strcmp(op, "-")) return "sub";
    if (0 == strcmp(op, "*")) return "mul";
    if (0 == strcmp(op, "/")) return "div";
    if (0 == strcmp(op, "%")) return "mod";
    if (0 == strcmp(op, "&")) return "bit_and";
    if (0 == strcmp(op, "|")) return "bit_or";
    if (0 == strcmp(op, "^")) return "bit_xor";
    if (0 == strcmp(op, "<<")) return "shl";
    if (0 == strcmp(op, ">>")) return "shr";
    if (0 == strcmp(op, "==")) return "eq";
    if (0 == strcmp(op, "!=")) return "eq"; // negated after call
    if (0 == strcmp(op, "<")) return "cmp";
    if (0 == strcmp(op, "<=")) return "cmp";
    if (0 == strcmp(op, ">")) return "cmp";
    if (0 == strcmp(op, ">=")) return "cmp";
    return null;
}

// Map compound assignment operator (e.g. "+=") to the trait function name
// by stripping the trailing '=' and delegating to binary_op_to_func_name.
static char const *compound_op_to_func_name(char const *op) {
    size_t len = strlen(op);
    if (len < 2 || op[len - 1] != '=') return null;
    char base[4];
    memcpy(base, op, len - 1);
    base[len - 1] = '\0';
    return binary_op_to_func_name(base);
}

// Map unary operator string to the trait function name. Returns null if not overloadable.
static char const *unary_op_to_func_name(char const *op) {
    if (0 == strcmp(op, "-")) return "neg";
    if (0 == strcmp(op, "!")) return "not";
    if (0 == strcmp(op, "~")) return "bit_not";
    return null;
}

// Check if a substituted monotype is a user-defined type (not a builtin).
static int is_user_defined_type(tl_monotype *mono) {
    if (!mono) return 0;
    if (!tl_monotype_is_inst(mono)) return 0;
    return str_is_empty(mono->cons_inst->def->c_type_name);
}

// Build the arity-mangled + module-mangled function name for operator dispatch.
// E.g., for module "Vec", function "add", arity 2: "Vec__add__2"
static str build_overload_func_name(allocator *alloc, str module, char const *func_name, u8 arity) {
    if (!str_is_empty(module)) {
        str safe_module = str_replace_char_str(alloc, module, '.', S("__"));
        return str_fmt(alloc, "%s__%s__%i", str_cstr(&safe_module), func_name, (int)arity);
    }
    return str_fmt(alloc, "%s__%i", func_name, (int)arity);
}

// Rewrite an operator node in-place to an NFA calling the overload function.
static void rewrite_op_to_nfa(tl_infer *self, ast_node *node, str func_name, ast_node **args, u8 n_args) {
    ast_node_rewrite_to_nfa(node, ast_node_create_sym(self->arena, func_name), args, n_args);
}

// Get parameter types for a function from the type environment.
static tl_monotype_sized get_func_param_types(tl_infer *self, str func_name) {
    tl_polytype *poly = tl_type_env_lookup(self->env, func_name);
    if (!poly) return (tl_monotype_sized){0};
    tl_monotype *arrow = poly->type;
    if (!tl_monotype_is_arrow(arrow)) return (tl_monotype_sized){0};
    return tl_monotype_arrow_get_args(arrow);
}

// Wrap argument with & if param_type is Ptr[...] and argument is not already a pointer.
// Analogous to ufcs_rewrite_call() implicit address-of (infer_constraint.c), but runs
// post-phase-4 where types are concrete — assigns types directly instead of via constraints.
static ast_node *maybe_wrap_address_of(tl_infer *self, ast_node *arg, tl_monotype *param_type) {
    if (!param_type || !tl_monotype_is_ptr(param_type)) return arg;
    if (!arg->type) return arg;
    tl_monotype *arg_type = arg->type->type;
    if (tl_monotype_is_ptr(arg_type)) return arg;

    ast_node *amp  = ast_node_create_sym_c(self->arena, "&");
    ast_node *addr = ast_node_create_unary_op(self->arena, amp, arg);
    addr->file     = arg->file;
    addr->line     = arg->line;
    addr->col      = arg->col;
    tl_monotype *ptr_type = tl_type_registry_ptr(self->registry, arg_type);
    amp->type  = tl_polytype_absorb_mono(self->arena, ptr_type);
    addr->type = tl_polytype_absorb_mono(self->arena, ptr_type);
    return addr;
}

// For standalone builtin types (CChar, CSize, CPtrDiff), return the trait family canonical
// module to try when a direct lookup in the type's own module fails. Returns empty for
// user-defined types or builtins that already map directly to their family (e.g. CInt → "Int").
str builtin_trait_family_module(tl_monotype *type) {
    if (str_is_empty(type->cons_inst->def->c_type_name)) return str_empty();
    int sc = type->cons_inst->def->integer_subchain;
    if (sc == TL_INTEGER_SUBCHAIN_CCHAR || sc == TL_INTEGER_SUBCHAIN_CPTRDIFF) return S("Int");
    if (sc == TL_INTEGER_SUBCHAIN_CSIZE) return S("UInt");
    return str_empty();
}

// Look up an overload function by operator name and arity. Returns the function name on
// success (allocated on self->arena), or empty string if not found.
// For builtin types, falls back to the trait family canonical module if the direct lookup misses.
static str find_overload_func(tl_infer *self, tl_monotype *type, char const *func_name, u8 arity) {
    str module = type->cons_inst->def->module;
    // Use transient arena for the lookup key to avoid leaking on miss.
    if (!str_is_empty(module)) {
        str lookup = build_overload_func_name(self->transient, module, func_name, arity);
        if (ast_node_str_map_get(self->toplevels, lookup)) return str_copy(self->arena, lookup);
    }
    // Family fallback: standalone builtin types (CChar, CSize, CPtrDiff) fall back to their
    // canonical family module (Int, UInt).
    str family = builtin_trait_family_module(type);
    if (!str_is_empty(family)) {
        str lookup = build_overload_func_name(self->transient, family, func_name, arity);
        if (ast_node_str_map_get(self->toplevels, lookup)) return str_copy(self->arena, lookup);
    }
    // CString fallback: Ptr[CChar] (= CString) checks the "CString" module.
    if (tl_monotype_is_ptr_to_char(type)) {
        str lookup = build_overload_func_name(self->transient, S("CString"), func_name, arity);
        if (ast_node_str_map_get(self->toplevels, lookup)) return str_copy(self->arena, lookup);
    }
    return str_empty();
}

// Map operator function name to its corresponding trait name for no_conform checking.
// Note: keep in sync with binary_op_to_func_name() and unary_op_to_func_name() above.
static char const *func_name_to_trait_name(char const *func) {
    if (0 == strcmp(func, "add")) return "Add";
    if (0 == strcmp(func, "sub")) return "Sub";
    if (0 == strcmp(func, "mul")) return "Mul";
    if (0 == strcmp(func, "div")) return "Div";
    if (0 == strcmp(func, "mod")) return "Mod";
    if (0 == strcmp(func, "bit_and")) return "BitAnd";
    if (0 == strcmp(func, "bit_or")) return "BitOr";
    if (0 == strcmp(func, "bit_xor")) return "BitXor";
    if (0 == strcmp(func, "shl")) return "Shl";
    if (0 == strcmp(func, "shr")) return "Shr";
    if (0 == strcmp(func, "eq")) return "Eq";
    if (0 == strcmp(func, "cmp")) return "Ord";
    if (0 == strcmp(func, "neg")) return "Neg";
    if (0 == strcmp(func, "not")) return "Not";
    if (0 == strcmp(func, "bit_not")) return "BitNot";
    return null;
}

// Check [[no_conform]] for an operator and emit an error if denied. Returns 1 if blocked.
static int check_no_conform_operator(tl_infer *self, ast_node *node, tl_monotype *type,
                                     char const *func_name, char const *op) {
    char const *trait = func_name_to_trait_name(func_name);
    if (!trait || !has_no_conform(self, type, trait)) return 0;
    str type_str = tl_monotype_to_string(self->transient, type);
    str msg =
      str_fmt(self->arena,
              "operator '%s' cannot be used on type %s: conformance to '%s' denied via [[no_conform(%s)]]",
              op, str_cstr(&type_str), trait, trait);
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_trait_bound_not_satisfied, .node = node, .message = msg}));
    return 1;
}

// DFS callback: rewrite operator nodes for user-defined types to function calls.
static void rewrite_operator_overloads(void *ctx, ast_node *node) {
    tl_infer *self = ctx;

    if (node->tag == ast_binary_op) {
        ast_node *left = node->binary_op.left;
        if (!left->type) return;

        tl_monotype *left_type = left->type->type;
        if (!is_user_defined_type(left_type)) return;

        char const *op        = str_cstr(&node->binary_op.op->symbol.name);
        int         is_neq    = (0 == strcmp(op, "!="));
        int         is_eq     = (0 == strcmp(op, "=="));
        char const *func_name = binary_op_to_func_name(op);
        if (!func_name) return;

        if (check_no_conform_operator(self, node, left_type, func_name, op)) return;

        str full_name = find_overload_func(self, left_type, func_name, 2);

        // For == and !=: fall back to cmp if eq is not found.
        if (str_is_empty(full_name) && (is_eq || is_neq)) {
            func_name = "cmp";
            if (check_no_conform_operator(self, node, left_type, func_name, op)) return;
            full_name = find_overload_func(self, left_type, func_name, 2);
        }

        if (str_is_empty(full_name)) return;

        // Capture operands before overwriting the union.
        ast_node  *right = node->binary_op.right;
        ast_node **args  = alloc_malloc(self->arena, 2 * sizeof(ast_node *));
        args[0]          = left;
        args[1]          = right;

        // Auto-address-of: wrap value args with & when function expects pointers.
        tl_monotype_sized param_types = get_func_param_types(self, full_name);
        if (param_types.size >= 2) {
            args[0] = maybe_wrap_address_of(self, args[0], param_types.v[0]);
            args[1] = maybe_wrap_address_of(self, args[1], param_types.v[1]);
        }

        if (is_neq && 0 != strcmp(func_name, "cmp")) {
            // a != b  →  !(eq(a, b))
            ast_node *eq_call =
              ast_node_create_nfa(self->arena, ast_node_create_sym(self->arena, full_name),
                                  (ast_node_sized){0}, (ast_node_sized){.v = args, .size = 2});
            eq_call->type          = node->type;

            tl_polytype *type      = node->type;
            node->tag              = ast_unary_op;
            node->unary_op.operand = eq_call;
            node->unary_op.op      = ast_node_create_sym_c(self->arena, "!");
            node->type             = type;
        } else if (0 == strcmp(func_name, "cmp")) {
            // a < b  →  cmp(a, b) < 0;  == via cmp: cmp(a, b) == 0
            char const  *cmp_op    = is_eq ? "==" : is_neq ? "!=" : op;
            tl_monotype *cint      = tl_type_registry_instantiate(self->registry, S("CInt"));
            tl_polytype *cint_poly = tl_polytype_absorb_mono(self->arena, cint);

            ast_node    *cmp_call =
              ast_node_create_nfa(self->arena, ast_node_create_sym(self->arena, full_name),
                                  (ast_node_sized){0}, (ast_node_sized){.v = args, .size = 2});
            cmp_call->type        = cint_poly;

            ast_node *zero        = ast_node_create_i64(self->arena, 0);
            zero->type            = cint_poly;

            tl_polytype *type     = node->type;
            node->binary_op.left  = cmp_call;
            node->binary_op.right = zero;
            node->binary_op.op    = ast_node_create_sym_c(self->arena, cmp_op);
            node->type            = type;
        } else {
            rewrite_op_to_nfa(self, node, full_name, args, 2);
        }

    } else if (node->tag == ast_reassignment_op) {
        ast_node *lhs = node->assignment.name;
        if (!lhs->type) return;

        tl_monotype *lhs_type = lhs->type->type;
        if (!is_user_defined_type(lhs_type)) return;

        char const *op        = str_cstr(&node->assignment.op->symbol.name);
        char const *func_name = compound_op_to_func_name(op);
        if (!func_name) return;

        if (check_no_conform_operator(self, node, lhs_type, func_name, op)) return;

        str full_name = find_overload_func(self, lhs_type, func_name, 2);
        if (str_is_empty(full_name)) return;

        // Capture fields before overwriting the union.
        ast_node  *value = node->assignment.value;
        ast_node **args  = alloc_malloc(self->arena, 2 * sizeof(ast_node *));
        args[0]          = lhs;
        args[1]          = value;

        tl_monotype_sized param_types = get_func_param_types(self, full_name);
        if (param_types.size >= 2) {
            args[0] = maybe_wrap_address_of(self, args[0], param_types.v[0]);
            args[1] = maybe_wrap_address_of(self, args[1], param_types.v[1]);
        }

        // Build NFA: func(lhs, value) — result type matches LHS
        ast_node *call = ast_node_create_nfa(self->arena, ast_node_create_sym(self->arena, full_name),
                                             (ast_node_sized){0}, (ast_node_sized){.v = args, .size = 2});
        call->type     = lhs->type;

        // Rewrite: ast_reassignment_op → ast_reassignment with value = func(lhs, value)
        node->tag                      = ast_reassignment;
        node->assignment.name          = lhs;
        node->assignment.value         = call;
        node->assignment.op            = null;
        node->assignment.is_field_name = 0;

    } else if (node->tag == ast_unary_op) {
        ast_node *operand = node->unary_op.operand;
        if (!operand->type) return;

        tl_monotype *operand_type = operand->type->type;
        if (!is_user_defined_type(operand_type)) return;

        char const *op        = str_cstr(&node->unary_op.op->symbol.name);
        char const *func_name = unary_op_to_func_name(op);
        if (!func_name) return;

        if (check_no_conform_operator(self, node, operand_type, func_name, op)) return;

        str full_name = find_overload_func(self, operand_type, func_name, 1);
        if (str_is_empty(full_name)) return;

        ast_node **args = alloc_malloc(self->arena, sizeof(ast_node *));
        args[0]         = operand;

        tl_monotype_sized param_types = get_func_param_types(self, full_name);
        if (param_types.size >= 1)
            args[0] = maybe_wrap_address_of(self, args[0], param_types.v[0]);

        rewrite_op_to_nfa(self, node, full_name, args, 1);
    }
}

// Rewrite operator overloads in all toplevel definitions.
// Called between Phase 4 (subs applied, types concrete) and Phase 5 (specialization).
void rewrite_operator_overloads_all(tl_infer *self) {
    hashmap_iterator iter = {0};
    ast_node        *node;
    while ((node = ast_node_str_map_iter(self->toplevels, &iter))) {
        // Skip generic templates — their operators are rewritten in post_specialize
        // after type substitution makes operand types concrete.
        if (ast_node_is_let(node) && node->let.n_type_parameters > 0) continue;
        ast_node_dfs(self, node, rewrite_operator_overloads);
    }
}

int post_specialize(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *special, tl_monotype *callsite) {
    // Do this after creating a specialised function
    ast_node *infer_target = get_infer_target(special);
    if (infer_target) {
        hires_timer st;
        int         stats = self->report_stats;

        // set result type into traverse_ctx
        if (callsite) {
            tl_monotype *result_type  = tl_monotype_arrow_result(callsite);
            traverse_ctx->result_type = result_type;
        }
        if (stats) self->counters.traverse_infer_calls++;
        if (stats) {
            hires_timer_init(&st);
            hires_timer_start(&st);
        }
        if (traverse_ast(self, traverse_ctx, infer_target, infer_traverse_cb)) {
            dbg_at(2, self, "note: post_specialize failed infer");
            return 1;
        }
        if (stats) {
            hires_timer_stop(&st);
            self->counters.specialize_infer_ms += hires_timer_elapsed_sec(&st) * 1000.0;
        }

        // Default weak integer literals created during re-inference, so that
        // specialization sees concrete Int/UInt for instance keys.
        tl_type_subs_default_weak_ints(self->subs, tl_type_registry_int(self->registry),
                                       tl_type_registry_uint(self->registry),
                                       tl_type_registry_float(self->registry));

        // Apply substitutions to AST before specialization, so types are concrete
        if (stats) hires_timer_start(&st);
        apply_subs_to_ast_node(self, infer_target);
        if (stats) {
            hires_timer_stop(&st);
            self->counters.specialize_subs_ms += hires_timer_elapsed_sec(&st) * 1000.0;
        }

        // Rewrite operator nodes on user-defined types to function calls (NFAs).
        // Must happen after subs are applied (types are concrete) and before
        // specialize_applications_cb (which processes the new NFA nodes).
        ast_node_dfs(self, infer_target, rewrite_operator_overloads);

        if (stats) self->counters.traverse_specialize_calls++;
        if (stats) hires_timer_start(&st);
        if (traverse_ast(self, traverse_ctx, infer_target, specialize_applications_cb)) {
            dbg_at(2, self, "note: post_specialize failed specialize");
            return 1;
        }
        if (stats) {
            hires_timer_stop(&st);
            self->counters.specialize_recurse_ms += hires_timer_elapsed_sec(&st) * 1000.0;
        }

#if DEBUG_INVARIANTS
        // Invariant: After specialization, all specialized NFA type arguments must be concrete
        check_specialized_nfa_type_args(self, infer_target, "post_specialize");
#endif
    }
    return 0;
}

// ============================================================================
// Trait bound conformance checking
// ============================================================================

// Check if a type has [[no_conform(trait_name)]] in its attributes.
static int has_no_conform(tl_infer *self, tl_monotype *concrete_type, char const *trait_generic_name) {
    str       type_name = concrete_type->cons_inst->def->name;
    ast_node *attrs     = str_map_get_ptr(self->attributes, type_name);
    if (!attrs || attrs->tag != ast_attribute_set) return 0;

    str trait_str = str_init_static(trait_generic_name);
    for (u8 i = 0; i < attrs->attribute_set.n; i++) {
        ast_node *attr = attrs->attribute_set.nodes[i];
        if (!ast_node_is_nfa(attr)) continue;
        str attr_name = ast_node_str(attr->named_application.name);
        if (!str_eq(attr_name, S("no_conform"))) continue;

        for (u8 j = 0; j < attr->named_application.n_arguments; j++) {
            ast_node *arg = attr->named_application.arguments[j];
            if (!ast_node_is_symbol(arg)) continue;
            if (str_eq(arg->symbol.name, trait_str)) return 1;
        }
    }
    return 0;
}

// Extract a trait name from a bound AST node (symbol or named function application).
static str trait_name_from_bound(ast_node *bound) {
    if (ast_node_is_symbol(bound)) return bound->symbol.name;
    if (ast_node_is_nfa(bound)) return ast_node_str(bound->named_application.name);
    return str_empty();
}

#define TRAIT_BOUND_MAX_DEPTH 16

// Push a trait-bound-not-satisfied error. Returns 1 (failure).
static int push_trait_error(tl_infer *self, ast_node *toplevel, str msg) {
    array_push(self->errors, ((tl_infer_error){
                               .tag = tl_err_trait_bound_not_satisfied, .node = toplevel, .message = msg}));
    return 1;
}

static int emit_trait_sig_error(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type,
                                str trait_name, str fn_name, str actual_sig, str expected_sig) {
    str type_str = tl_monotype_to_string(self->transient, concrete_type);
    return push_trait_error(
      self, toplevel,
      str_fmt(self->arena, "type %s does not satisfy trait %s: function '%s' has signature %s, expected %s",
              str_cstr(&type_str), str_cstr(&trait_name), str_cstr(&fn_name), str_cstr(&actual_sig),
              str_cstr(&expected_sig)));
}

// Check that a function's full arrow type matches the trait signature's expected arrow.
// Returns 0 on success, 1 on failure (error pushed).
static int check_trait_arrow(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type, str trait_name,
                             tl_trait_sig *sig, tl_trait_def *trait, str func_name) {
    if (!sig->arrow) return 0;

    tl_polytype *poly = tl_type_env_lookup(self->env, func_name);
    if (!poly) return 0;

    tl_monotype *actual_arrow = poly->type;
    if (!tl_monotype_is_arrow(actual_arrow)) return 0;

    // Determine trait's type parameter name.
    // Built-in traits (source_node == null) use "T".
    // User-defined traits: first type argument name from the definition AST.
    str type_param = S("T");
    if (trait->source_node && trait->source_node->trait_def.n_type_arguments > 0)
        type_param = ast_node_str(trait->source_node->trait_def.type_arguments[0]);

    // Parse the expected arrow AST with the trait's type parameter mapped to the concrete type.
    hot_parse_ctx_reinit(self, null);
    str_map_set_ptr(&self->hot_parse_ctx.type_arguments, type_param, concrete_type);
    tl_monotype *expected_arrow =
      tl_type_registry_parse_type_with_ctx(self->registry, sig->arrow, &self->hot_parse_ctx);
    self->hot_parse_ctx_guard = 0;
    if (!expected_arrow || !tl_monotype_is_arrow(expected_arrow)) return 0;

    // Resolve the actual arrow to a concrete type.
    // If the function is generic, instantiate its quantifiers with the concrete
    // type's type arguments so that type parameters become concrete types.
    tl_monotype      *actual_resolved;
    tl_monotype_sized type_args = concrete_type->cons_inst->args;
    if (poly->quantifiers.size > 0) {
        if (type_args.size == 0 && poly->quantifiers.size == 1) {
            // Generic trait function on a nullary type (e.g. Float.to_string[T] for CLongDouble):
            // instantiate the single quantifier with the concrete type itself.
            type_args = (tl_monotype_sized){.v = &concrete_type, .size = 1};
        }
        actual_resolved = tl_polytype_instantiate_for_type(self->transient, poly, type_args, self->subs);
    } else {
        actual_resolved = tl_monotype_clone(self->transient, actual_arrow);
    }
    tl_monotype_substitute(self->transient, actual_resolved, self->subs, null);

    // Compare full arrows by hash.
    if (tl_monotype_hash64(expected_arrow) != tl_monotype_hash64(actual_resolved)) {
        // Auto-address-of fallback: accept Ptr[T] or Ptr[Const[T]] params
        // where expected has T, with matching return type.
        int compatible = 0;
        tl_monotype_sized ap = tl_monotype_arrow_get_args(actual_resolved);
        tl_monotype_sized ep = tl_monotype_arrow_get_args(expected_arrow);
        if (ap.size == ep.size) {
            compatible = 1;
            for (u32 j = 0; j < ap.size; j++) {
                u64 eh = tl_monotype_hash64(ep.v[j]);
                if (tl_monotype_hash64(ap.v[j]) == eh) continue;
                if (tl_monotype_is_ptr(ap.v[j])) {
                    tl_monotype *target = tl_monotype_strip_const(tl_monotype_ptr_target(ap.v[j]));
                    if (tl_monotype_hash64(target) == eh) continue;
                }
                compatible = 0;
                break;
            }
            if (compatible) {
                u64 arh = tl_monotype_hash64(tl_monotype_arrow_result(actual_resolved));
                u64 erh = tl_monotype_hash64(tl_monotype_arrow_result(expected_arrow));
                if (arh != erh) compatible = 0;
            }
        }
        if (!compatible) {
            str actual_str   = tl_monotype_to_string(self->transient, actual_resolved);
            str expected_str = tl_monotype_to_string(self->transient, expected_arrow);
            return emit_trait_sig_error(self, toplevel, concrete_type, trait_name, sig->name, actual_str,
                                        expected_str);
        }
    }
    return 0;
}

static int emit_trait_bound_error(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type,
                                  str trait_name, str fn_name, u32 arity) {
    str type_str = tl_monotype_to_string(self->transient, concrete_type);
    return push_trait_error(
      self, toplevel,
      str_fmt(self->arena, "type %s does not satisfy trait %s: missing function '%s' with arity %u",
              str_cstr(&type_str), str_cstr(&trait_name), str_cstr(&fn_name), (unsigned)arity));
}

static int emit_no_conform_bound_error(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type,
                                       str trait_name, char const *trait_generic_name) {
    str type_str = tl_monotype_to_string(self->transient, concrete_type);
    return push_trait_error(
      self, toplevel,
      str_fmt(self->arena,
              "type %s does not satisfy trait %s: conformance explicitly denied via [[no_conform(%s)]]",
              str_cstr(&type_str), str_cstr(&trait_name), trait_generic_name));
}

// Check that a concrete type satisfies a trait bound. Returns 0 on success, 1 on failure.
// On failure, pushes an error to self->errors.
static int check_trait_bound_(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type,
                              str trait_name, int depth) {

    if (depth >= TRAIT_BOUND_MAX_DEPTH) return 0;

    tl_trait_def *trait = str_map_get_ptr(self->traits, trait_name);
    if (!trait) return 0; // Unknown trait — skip (may be a type, not a trait)

    if (!tl_monotype_is_inst(concrete_type)) return 0; // Not a concrete inst type — skip

    // Check for [[no_conform(Trait)]] on the type
    if (has_no_conform(self, concrete_type, str_cstr(&trait->generic_name)))
        return emit_no_conform_bound_error(self, toplevel, concrete_type, trait_name,
                                           str_cstr(&trait->generic_name));

    // Branch on whether both the type AND the trait are builtins. Compiler-provided traits
    // (Add, Eq, Hash, etc.) on builtin types use the hardcoded capability table. All other
    // combinations (user-defined traits on builtins, any trait on user types) use function lookup.
    int is_builtin_type = !is_user_defined_type(concrete_type) || tl_monotype_is_ptr_to_char(concrete_type);
    int is_builtin_trait = !trait->source_node;

    if (is_builtin_type && is_builtin_trait) {
        // Built-in traits on built-in types: check intrinsic support per signature.
        int is_integer = tl_monotype_is_integer_convertible(concrete_type);
        int is_float   = tl_monotype_is_float_convertible(concrete_type);
        int is_numeric = is_integer || is_float;
        int is_bool    = str_eq(concrete_type->cons_inst->def->name, S("Bool"));
        for (u32 i = 0; i < trait->sigs.size; i++) {
            str fn = trait->sigs.v[i].name;
            int ok = 0;
            // Arithmetic: add, sub, mul, div, neg — numeric types; mod — integer only
            if (str_eq(fn, S("add")) || str_eq(fn, S("sub")) || str_eq(fn, S("mul")) ||
                str_eq(fn, S("div")) || str_eq(fn, S("neg")))
                ok = is_numeric;
            else if (str_eq(fn, S("mod"))) ok = is_integer;
            // Bitwise: bit_and, bit_or, bit_xor, shl, shr, bit_not — integer types
            else if (str_eq(fn, S("bit_and")) || str_eq(fn, S("bit_or")) || str_eq(fn, S("bit_xor")) ||
                     str_eq(fn, S("shl")) || str_eq(fn, S("shr")) || str_eq(fn, S("bit_not")))
                ok = is_integer;
            // Comparison: eq, cmp — numeric types and Bool
            else if (str_eq(fn, S("eq")) || str_eq(fn, S("cmp"))) ok = is_numeric || is_bool;
            // Logical: not — Bool
            else if (str_eq(fn, S("not"))) ok = is_bool;
            if (!ok)
                return emit_trait_bound_error(self, toplevel, concrete_type, trait_name, fn,
                                              trait->sigs.v[i].arity);
        }
    } else {
        // Function lookup path: user-defined traits on any type, or builtin traits on user types.
        tl_monotype_sized type_args = concrete_type->cons_inst->args;
        for (u32 i = 0; i < trait->sigs.size; i++) {
            tl_trait_sig *sig = &trait->sigs.v[i];
            str func_name     = find_overload_func(self, concrete_type, str_cstr(&sig->name), sig->arity);
            // Ptr[T] auto-deref: if direct lookup failed and type is Ptr[T], try T's module.
            // Mirrors ufcs_rewrite_call() which auto-derefs pointer receivers.
            if (str_is_empty(func_name) && tl_monotype_is_ptr(concrete_type)) {
                tl_monotype *target = tl_monotype_strip_const(tl_monotype_ptr_target(concrete_type));
                if (tl_monotype_is_inst(target))
                    func_name = find_overload_func(self, target, str_cstr(&sig->name), sig->arity);
            }
            if (!str_is_empty(func_name)) {
                // Direct implementation — check full arrow conformance.
                if (check_trait_arrow(self, toplevel, concrete_type, trait_name, sig, trait, func_name))
                    return 1;
            } else if (str_eq(sig->name, S("eq")) && sig->arity == 2) {
                // eq is derivable from cmp: Ord inherits Eq, so a type with only cmp
                // can satisfy Eq. Skip arrow check — the compiler synthesizes the Bool wrapper.
                func_name = find_overload_func(self, concrete_type, "cmp", 2);
            }
            if (str_is_empty(func_name))
                return emit_trait_bound_error(self, toplevel, concrete_type, trait_name, sig->name,
                                              sig->arity);

            // Conditional conformance: if the conforming function has bounded type parameters,
            // verify those bounds recursively using the concrete type's type arguments.
            ast_node *func_top = toplevel_get(self, func_name);
            if (!func_top || !ast_node_is_let(func_top)) continue;
            u32 n_tp = func_top->let.n_type_parameters;
            if (n_tp == 0) continue;

            for (u32 j = 0; j < n_tp; j++) {
                ast_node *tp = func_top->let.type_parameters[j];
                if (!ast_node_is_symbol(tp)) continue;
                ast_node *bound = tp->symbol.annotation;
                if (!bound) continue;

                // Map the function's j-th type parameter to the concrete type's j-th type argument
                if (j >= type_args.size) continue;
                tl_monotype *inner_concrete = type_args.v[j];
                if (!inner_concrete || !tl_monotype_is_inst(inner_concrete)) continue;

                str inner_trait = trait_name_from_bound(bound);
                if (str_is_empty(inner_trait)) continue;

                if (check_trait_bound_(self, toplevel, inner_concrete, inner_trait, depth + 1)) return 1;
            }
        }
    }

    // Check parent traits (trait inheritance)
    forall(i, trait->parents) {
        if (check_trait_bound_(self, toplevel, concrete_type, trait->parents.v[i], depth + 1)) return 1;
    }

    return 0;
}

int check_trait_bound(tl_infer *self, ast_node *toplevel, tl_monotype *concrete_type, str trait_name) {
    return check_trait_bound_(self, toplevel, concrete_type, trait_name, 0);
}

// Resolve the concrete type for the i-th type parameter from the arrow or resolved_type_args.
static tl_monotype *resolve_type_param_concrete(tl_infer *self, ast_node *toplevel, tl_monotype *arrow,
                                                tl_monotype_sized resolved_type_args, u32 i) {
    // First try resolved_type_args (explicit type arguments at call site)
    if (i < resolved_type_args.size && resolved_type_args.v[i]) {
        tl_monotype *m = resolved_type_args.v[i];
        if (!tl_monotype_is_concrete(m)) {
            m = tl_monotype_clone(self->transient, m);
            tl_monotype_substitute(self->transient, m, self->subs, null);
        }
        if (tl_monotype_is_concrete(m)) return m;
    }

    // Fall back: match the type parameter against the arrow's parameter types.
    // The type parameter appears in the function's value parameters. Find the first
    // value parameter whose original annotation references this type parameter,
    // and read the corresponding concrete type from the arrow.
    if (!tl_monotype_is_arrow(arrow)) return null;
    tl_monotype_sized arrow_args = arrow->list.xs.v[0]->list.xs;
    ast_node         *tp         = toplevel->let.type_parameters[i];
    str               tp_name    = tp->symbol.name;

    u32               n_params   = toplevel->let.n_parameters;
    for (u32 j = 0; j < n_params && j < arrow_args.size; j++) {
        ast_node *param = toplevel->let.parameters[j];
        if (!ast_node_is_symbol(param)) continue;
        // Check if this parameter's annotation references the type parameter
        ast_node *ann = param->symbol.annotation;
        if (ann && ast_node_is_symbol(ann) && str_eq(ann->symbol.name, tp_name)) {
            return arrow_args.v[j];
        }
    }
    return null;
}

// Verify trait bounds on a generic function's type parameters against resolved concrete types.
// Returns 0 on success, 1 if any bound is not satisfied.
static int verify_trait_bounds(tl_infer *self, ast_node *toplevel, tl_monotype *arrow,
                               tl_monotype_sized resolved_type_args) {
    if (!ast_node_is_let(toplevel)) return 0;
    u32 n_tp = toplevel->let.n_type_parameters;
    if (n_tp == 0) return 0;

    for (u32 i = 0; i < n_tp; i++) {
        ast_node *tp = toplevel->let.type_parameters[i];
        if (!ast_node_is_symbol(tp)) continue;
        ast_node *bound = tp->symbol.annotation;
        if (!bound) continue;

        tl_monotype *concrete = resolve_type_param_concrete(self, toplevel, arrow, resolved_type_args, i);
        if (!concrete) continue;

        str trait_name = trait_name_from_bound(bound);
        if (str_is_empty(trait_name)) continue;

        if (check_trait_bound(self, toplevel, concrete, trait_name)) return 1;
    }
    return 0;
}

// ============================================================================
// Arrow specialization
// ============================================================================

str specialize_arrow(tl_infer *self, traverse_ctx *traverse_ctx, str name, tl_monotype *arrow,
                     tl_monotype_sized resolved_type_args) {

    if (!tl_monotype_is_concrete_no_weak(arrow))
        tl_monotype_substitute(self->arena, arrow, self->subs, null);

    // 1. Check if already specialized
    if (instance_name_exists(self, name)) {
        if (self->report_stats) self->counters.specialize_already++;
        return name;
    }

    // 2. Cache lookup using pre-resolved monotypes
    str *found = instance_lookup_arrow(self, name, arrow, resolved_type_args);
    if (found) {
        if (self->report_stats) self->counters.specialize_cache_hits++;
        return *found;
    }

    // 2a. Check that name is valid
    ast_node *toplevel = toplevel_get(self, name);
    if (!toplevel) return str_empty();

    // 2b. Verify trait bounds on type parameters
    if (verify_trait_bounds(self, toplevel, arrow, resolved_type_args)) return str_empty();

    // 3. Create unique instance name(e.g., "identity_0")
    name_and_type key       = make_instance_key(self, name, arrow, resolved_type_args);
    str           inst_name = next_instantiation(self, name);
    instance_add(self, &key, inst_name);
    if (self->report_stats) self->counters.specialize_created++;

    // 4. Clone generic function's AST
    hires_timer st;
    if (self->report_stats) {
        hires_timer_init(&st);
        hires_timer_start(&st);
    }
    arena_watermark clone_wm = arena_save(self->transient);
    ast_node       *generic_node =
      clone_generic_for_arrow(self, toplevel, arrow, inst_name,
                              traverse_ctx ? traverse_ctx->type_arguments : null, resolved_type_args);
    arena_restore(self->transient, clone_wm);
    if (self->report_stats) {
        hires_timer_stop(&st);
        self->counters.specialize_clone_ms += hires_timer_elapsed_sec(&st) * 1000.0;
    }

    // 5. Add to environment and toplevel
    specialized_add_to_env(self, inst_name, arrow);
    toplevel_add(self, inst_name, generic_node);
    tl_infer_set_attributes(self, toplevel_name_node(generic_node));
    dbg_at(2, self, "toplevel_add: %s", str_cstr(&inst_name));

    // 6. CRITICAL: Process the specialized function body
    ast_node *special = toplevel_get(self, inst_name);
    if (post_specialize(self, traverse_ctx, special, arrow)) return str_empty();
    return inst_name;
}

static int specialize_arrow_with_name(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *fun_name_node,
                                      tl_monotype *callsite, tl_monotype_sized resolved_type_args) {
    if (!tl_monotype_is_arrow(callsite)) return 0;

    str instance_name =
      specialize_arrow(self, traverse_ctx, ast_node_str(fun_name_node), callsite, resolved_type_args);
    if (str_is_empty(instance_name)) return 1;
    ast_node_name_replace(fun_name_node, instance_name);
    return 0;
}

// ============================================================================
// Per-node specialization
// ============================================================================

static int specialize_operand(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    // Here we handle function pointers in operand positions. When this is called after the function being
    // pointed to has been specialised, the arrow types will be concrete. We use those types to look up
    // (using specialize_arrow) the specialised version and replace the symbol name with the specialised
    // name. This ensures the transpiler refers to an existant concrete function rather than the generic
    // template.

    tl_polytype *value_type = node->type;

    if (!value_type || !tl_monotype_is_arrow(value_type->type)) return 0;
    if (!tl_polytype_is_concrete(value_type)) return 0;
    if (!ast_node_is_symbol(node)) return 0;

    str value_name = ast_node_str(node);
    // TODO: function pointers with callsite type arguments
    str inst_name =
      specialize_arrow(self, traverse_ctx, value_name, value_type->type, (tl_monotype_sized){0});
    if (str_is_empty(inst_name)) return 0; // FIXME: ignores error
    ast_node_name_replace(node, inst_name);
    return 0;
}

// For let-in lambdas whose name type is a polymorphic scheme (e.g., [[alloc]] closures
// returning a generic identity function): add_generic generalizes the name, but the body
// symbol carries the concrete instantiation after substitution.  Create the specialization
// from the body type directly.
static int specialize_let_in_lambda_from_body(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    tl_polytype *body_type = node->let_in.body->type;
    if (!body_type || !tl_polytype_is_concrete(body_type) || !tl_monotype_is_arrow(body_type->type))
        return 0;

    tl_monotype *arrow     = body_type->type;
    str          name      = ast_node_str(node->let_in.name);
    str          inst_name = specialize_arrow(self, traverse_ctx, name, arrow, (tl_monotype_sized){0});

    if (!str_is_empty(inst_name)) {
        ast_node_name_replace(node->let_in.name, inst_name);
        // Also update the body symbol to reference the specialized name.
        // The body may be a plain symbol or an ast_body wrapping expressions.
        // FIXME: this is a ridiculous special case that only fixes the test
        // test_alloc_closure_no_captures.tl
        ast_node *body_sym = node->let_in.body;
        if (ast_node_is_body(body_sym) && body_sym->body.expressions.size > 0)
            body_sym = body_sym->body.expressions.v[body_sym->body.expressions.size - 1];
        if (ast_node_is_symbol(body_sym)) ast_node_name_replace(body_sym, inst_name);
    }
    return 0;
}

// For let-in lambdas with a concrete name type: look up the specialization that was created
// when the body's call sites were processed and rename the binding to match.
static int specialize_let_in_lambda_lookup(tl_infer *self, ast_node *node, tl_polytype *name_type) {
    tl_monotype *arrow = name_type->type;
    str          name  = ast_node_str(node->let_in.name);

    // Resolve any remaining weak ints so the hash matches the call-site specialization.
    // First apply substitutions, then default any weak ints that weren't resolved by
    // substitution (their TVs may not have been registered in the subs map).
    if (!tl_monotype_is_concrete_no_weak(arrow))
        tl_monotype_substitute(self->arena, arrow, self->subs, null);
    if (!tl_monotype_is_concrete_no_weak(arrow))
        tl_monotype_default_weak_ints(arrow, tl_type_registry_int(self->registry),
                                      tl_type_registry_uint(self->registry),
                                      tl_type_registry_float(self->registry));

    str *found = instance_lookup_arrow(self, name, arrow, (tl_monotype_sized){0});
    if (found) ast_node_name_replace(node->let_in.name, *found);
    return 0;
}

static int specialize_let_in(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_let_in(node));
    tl_polytype *name_type = node->let_in.name->type;
    int          concrete  = name_type && tl_polytype_is_concrete(name_type);

    // Non-concrete name with body: only lambdas need handling (via body type).
    // Non-lambda non-concrete let-ins with a body are no-ops.
    if (!concrete && node->let_in.body) {
        if (ast_node_is_let_in_lambda(node))
            return specialize_let_in_lambda_from_body(self, traverse_ctx, node);
        return 0;
    }

    // Let-in lambda with concrete arrow: look up specialization created by call sites.
    if (ast_node_is_let_in_lambda(node) && tl_monotype_is_arrow(name_type->type))
        return specialize_let_in_lambda_lookup(self, node, name_type);

    // Non-lambda let-in (or lambda without arrow type): specialize the bound value.
    return specialize_operand(self, traverse_ctx, node->let_in.value);
}

static int specialize_reassignment(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_assignment(node));
    return specialize_operand(self, traverse_ctx, node->assignment.value);
}

static int specialize_case(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    assert(ast_node_is_case(node));

    // Handle tagged union case: specialize variant types in conditions
    if (node->case_.is_union) {
        forall(i, node->case_.conditions) {
            ast_node *cond = node->case_.conditions.v[i];

            // Skip nil condition (else arm)
            if (ast_node_is_nil(cond)) continue;

            // Condition should be a symbol with annotation_type set by infer_case
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation_type) continue;

            tl_monotype *variant_type = cond->symbol.annotation_type->type;

            // Handle pointer types (for mutable case bindings)
            tl_monotype *inner_type = variant_type;
            if (tl_monotype_is_ptr(variant_type)) {
                inner_type = tl_monotype_ptr_target(variant_type);
            }

            // If the variant type is a generic inst that needs specialization
            if (tl_monotype_is_inst(inner_type) && !tl_monotype_is_inst_specialized(inner_type)) {
                str               generic_name = inner_type->cons_inst->def->generic_name;
                tl_monotype_sized args         = inner_type->cons_inst->args;
                tl_polytype      *special_type = null;
                str               inst_name    = str_empty();

                if (!tl_monotype_is_concrete(inner_type)) {
                    // Variant type has unresolved type variables (e.g., from a case annotation
                    // where the expression type wasn't concrete during inference). Re-derive
                    // concrete variant types from the expression's now-concrete wrapper type.
                    tl_monotype *expr_type =
                      node->case_.expression->type ? node->case_.expression->type->type : null;
                    if (expr_type) tl_monotype_substitute(self->arena, expr_type, self->subs, null);

                    if (expr_type && tl_monotype_is_concrete(expr_type) && tl_monotype_is_inst(expr_type)) {
                        str          variant_name = ast_node_name_original(cond->symbol.annotation);
                        tl_monotype *concrete_variant =
                          tagged_union_find_variant(expr_type, variant_name, null);
                        if (concrete_variant && tl_monotype_is_inst(concrete_variant)) {
                            generic_name = concrete_variant->cons_inst->def->generic_name;
                            args         = concrete_variant->cons_inst->args;
                        }
                    }
                }

                inst_name = specialize_type_constructor(self, generic_name, args, &special_type);
                if (!str_is_empty(inst_name) && special_type) {
                    // Update the annotation_type with the specialized type
                    if (tl_monotype_is_ptr(variant_type)) {
                        tl_monotype *new_ptr =
                          tl_type_registry_ptr(self->registry, tl_polytype_concrete(special_type));
                        cond->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, new_ptr);
                    } else {
                        cond->symbol.annotation_type = special_type;
                    }
                    ast_node_type_set(cond, cond->symbol.annotation_type);

                    // Update the annotation node's name to the specialized name
                    if (cond->symbol.annotation && ast_node_is_symbol(cond->symbol.annotation)) {
                        ast_node_name_replace(cond->symbol.annotation, inst_name);
                    }
                }
            }
        }
        return 0;
    }

    // Handle binary predicate case (non-union)
    if (!node->case_.binary_predicate) return 0;

    ast_node *predicate = node->case_.binary_predicate;
    if (!ast_node_is_symbol(predicate)) return 0; // FIXME: what about lambdas?
    if (!node->case_.conditions.size) return 0;

    tl_polytype *pred_arrow =
      make_binary_predicate_arrow(self, traverse_ctx, node->case_.expression, node->case_.conditions.v[0]);

    str predicate_name = ast_node_str(predicate);
    str inst_name =
      specialize_arrow(self, traverse_ctx, predicate_name, pred_arrow->type, (tl_monotype_sized){0});

    if (str_is_empty(inst_name)) return 0; // FIXME: ignores error
    ast_node_name_replace(predicate, inst_name);

    return 0;
}

void specialize_type_alias(tl_infer *self, ast_node *node) {
    // if target of type alias is a generic instantiation, ensure it it is properly specialized.
    // load_toplevel will have generalized the target type and inserted it into the alias registry.
    // we will need to specialize it and replace it.
    assert(ast_node_is_type_alias(node));

    ast_node    *target = node->type_alias.target;
    tl_monotype *parsed = tl_type_registry_parse_type(self->registry, target);
#if DEBUG_TYPE_ALIAS
    {
        str name_dbg = toplevel_name(node);
        str type_dbg = tl_monotype_to_string(self->transient, parsed);
        fprintf(stderr,
                "[DEBUG_TYPE_ALIAS] specialize_type_alias: '%s' parsed=%s is_inst=%d is_concrete=%d\n",
                str_cstr(&name_dbg), str_cstr(&type_dbg), tl_monotype_is_inst(parsed),
                tl_monotype_is_concrete(parsed));
    }
#endif
    if (tl_monotype_is_inst(parsed) && tl_monotype_is_concrete(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg_at(2, self, "specialize_type_alias: %s = %s", str_cstr(&name), str_cstr(&tmp));
        tl_type_registry_type_alias_insert(self->registry, name,
                                           tl_polytype_absorb_mono(self->arena, parsed));
        assert(tl_polytype_is_concrete(tl_type_registry_get(self->registry, name)));
    } else if (tl_monotype_is_inst(parsed)) {
        str name = toplevel_name(node);
        str tmp  = tl_monotype_to_string(self->transient, parsed);
        dbg_at(2, self, "specialize_type_alias: not concrete: %s = %s", str_cstr(&name), str_cstr(&tmp));
    }
}

static int is_toplevel_function_name(tl_infer *self, ast_node *arg) {
    str       arg_name = ast_node_str(arg);
    ast_node *top      = toplevel_get(self, arg_name);
    if (!top) return 0;
    if (top->type && !tl_monotype_is_arrow(top->type->type)) return 0;
    return 1;
}

static int specialize_value_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                      tl_monotype_sized expected_types) {
    // Visits arguments to check for symbols referring to toplevel functions.
    // When found, specializes the function according to the expected type.

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    u32                i = 0;
    while ((arg = ast_arguments_next(&iter))) {

        if (ast_node_is_assignment(arg))
            if (specialize_reassignment(self, traverse_ctx, arg)) return 1;

        // Handle let_in_lambda arguments: specialize the lambda via its name
        if (ast_node_is_let_in_lambda(arg)) {
            ast_node *name_node = arg->let_in.name;

            if (!ast_node_is_symbol(name_node)) fatal("runtime error");
            if (i >= expected_types.size) fatal("runtime error");

            tl_monotype *expected = expected_types.v[i];
            if (!tl_monotype_is_arrow(expected)) fatal("runtime error");

            str old_name = name_node->symbol.name;

            // Specialize the lambda argument
            if (specialize_arrow_with_name(self, traverse_ctx, name_node, expected, (tl_monotype_sized){0}))
                return 1;

            str new_name = name_node->symbol.name;

            // Update body references to use the specialized name: recall that hoisted lambdas have a unique
            // body: just a symbol with the mangled name of the hoised function. See
            // parser.c:maybe_wrap_lambda_function_in_let_in
            if (!str_eq(old_name, new_name)) {
                ast_node *body = arg->let_in.body;

                if (!ast_node_is_body(body)) fatal("runtime error");
                forall(j, body->body.expressions) {
                    ast_node *expr = body->body.expressions.v[j];
                    if (ast_node_is_symbol(expr) && str_eq(expr->symbol.name, old_name))
                        ast_node_name_replace(expr, new_name);
                }
            }

            goto next;
        }

        if (!ast_node_is_symbol(arg)) goto next;
        if (!is_toplevel_function_name(self, arg)) goto next;
        if (i >= expected_types.size) fatal("runtime error");
        if (specialize_arrow_with_name(self, traverse_ctx, arg, expected_types.v[i],
                                       (tl_monotype_sized){0}))
            return 1;

    next:
        ++i;
    }
    return 0;
}

static int specialize_arguments(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node,
                                tl_monotype *arrow) {
    // Visits arguments used in node (function call arguments, etc) to check for symbols which refer to
    // toplevel functions. When found, that function is specialized according to the callsite's expected
    // type.

    tl_monotype_sized app_args = tl_monotype_arrow_args(arrow)->list.xs;
    return specialize_value_arguments(self, traverse_ctx, node, app_args);
}

// ============================================================================
// Generic function specialization (Phase 5)
// ============================================================================
//
// Top-down pass that monomorphizes generic functions. For each call site with
// concrete argument types, clones the generic definition, re-infers the clone at
// the concrete type, and registers it as a new top-level.  Handles type
// constructor specialization, user-type specialization, and recursive descent
// through let-in, case, and operand nodes.

int specialize_applications_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (ast_node_is_nfa(node)) {
        str name = ast_node_str(node->named_application.name);

        dbg(self, "specialize_applications_cb: enter '%s'", str_cstr(&name));
    }

    // Important: resolve the node, so that traverse_ctx is properly updated, including type arguments. For
    // example, node could be a symbol which is a formal argument that carries an annotation referring to a
    // type variable.
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) {
        return 1;
    }

    // check for nullary type constructors — but not formal parameters (let-in names are handled
    // by specialize_let_in after child traversal, when call sites have established concrete types)
    if (ast_node_is_symbol(node)) {
        if (traverse_ctx->node_pos != npos_formal_parameter)
            return specialize_operand(self, traverse_ctx, node);
        return 0;
    }
    // check for let_in nodes and assignments
    if (ast_node_is_let_in(node)) return specialize_let_in(self, traverse_ctx, node);
    if (ast_node_is_assignment(node)) return specialize_reassignment(self, traverse_ctx, node);
    if (ast_node_is_case(node)) return specialize_case(self, traverse_ctx, node);

    // check for type predicate
    if (ast_node_is_type_predicate(node)) return check_type_predicate(self, traverse_ctx, node);

    int is_anon = ast_node_is_lambda_application(node);

    // or else the remainder of this function handles nfas and anon lambda applications
    if (!ast_node_is_nfa(node) && !ast_node_is_lambda_application(node)) return 0;

    if (ast_node_is_specialized(node)) return 0;

    tl_polytype *callsite = null;
    if (!is_anon) {
        str name = ast_node_str(node->named_application.name);

        if (self->verbose >= 3) {
            str tmp = v2_ast_node_to_string(self->transient, node);
            dbg(self, "specialize_applications_cb: nfa '%s'", str_cstr(&tmp));
            str_deinit(self->transient, &tmp);
        }

        // do not process intrinsic calls or their arguments
        if (is_intrinsic(name)) return 0;

        // may be too early, e.g. for pointers
        // but type aliases and type constructors are always available
        if (!toplevel_get(self, name) && !tl_type_registry_exists(self->registry, name)) {
            dbg(self, "specialize_applications_cb: skipping '%s'", str_cstr(&name));

            return 0; // too early
        }

        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) {
            // check if it's a type alias or type constructor in the registry
            type = tl_type_registry_get(self->registry, name);
            if (!type) return 0; // mutual recursion or variable holding function pointer
        }

        // divert if this is a type constructor
        if (tl_polytype_is_type_constructor(type)) return specialize_user_type(self, traverse_ctx, node);

        // remember this callsite is specialized
        ast_node_set_is_specialized(node);

        // if the generic function type is concrete with no weak vars and no arrow args, use its type rather
        // than callsite, because the callsite should not override any concrete constraints identified at
        // the time the function is defined.
        if (tl_polytype_is_concrete_no_weak(type)) {
            // however, if any of the args is an arrow type, it must follow the non-concrete path
            if (!tl_monotype_arrow_has_arrow(type->type)) {
                callsite = type;
                {
                    str tmp = tl_polytype_to_string(self->transient, type);
                    dbg(
                      self,
                      "specialize_applications_cb: type is concrete, ignoring callsite type. Concrete : %s",
                      str_cstr(&tmp));
                    str_deinit(self->transient, &tmp);
                }
            }
        }

        if (!callsite) {
            if (node->named_application.is_function_reference) {
                // Function reference with explicit type args: use the resolved function type
                // as the callsite instead of constructing an arrow from (empty) value arguments.
                if (!node->type) return 1;
                if (!tl_monotype_is_concrete_no_weak(node->type->type))
                    tl_monotype_substitute(self->arena, node->type->type, self->subs, null);
                callsite = node->type;
            } else {
                // Important: use _with variant to copy free variables info to the arrow, which is added to
                // the environment further down.
                callsite = make_arrow_with(self, traverse_ctx, node, type);
                if (!callsite) {
                    return 1;
                }
            }
        }

#if DEBUG_RECURSIVE_TYPES
        {
            str call_str = tl_polytype_to_string(self->transient, callsite);
            fprintf(
              stderr,
              "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: name='%s' callsite=%s concrete=%d\n",
              str_cstr(&name), str_cstr(&call_str), tl_polytype_is_concrete(callsite));
        }
#endif

        // Specialize type constructors appearing in explicit type arguments.
        // E.g., sizeof[Point[Int]]() needs Point[Int] specialized to Point_8.
#if DEBUG_RECURSIVE_TYPES
        if (node->named_application.n_type_arguments > 0) {
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: '%s' has %u explicit type args\n",
                    str_cstr(&name), node->named_application.n_type_arguments);
        }
#endif
        for (u32 i = 0; i < node->named_application.n_type_arguments; i++) {
            ast_node *type_arg = node->named_application.type_arguments[i];
            if (ast_node_is_nfa(type_arg) &&
                0 == type_literal_specialize(self, type_arg,
                                             traverse_ctx ? traverse_ctx->type_arguments : null)) {
                // type_literal_specialize sets the type on the NFA's name node, but
                // concretize_params and traverse_ctx_assign_type_arguments check the NFA
                // node itself. Propagate the type up so both paths find it.
                if (!type_arg->type && type_arg->named_application.name->type) {
                    ast_node_type_set(type_arg, type_arg->named_application.name->type);
                }

#if DEBUG_RECURSIVE_TYPES
                {
                    str ta_str = type_arg->type ? tl_polytype_to_string(self->transient, type_arg->type)
                                                : str_init(self->transient, "(null)");
                    fprintf(stderr, "[DEBUG_RECURSIVE_TYPES]   type_arg[%u] after specialize: %s\n", i,
                            str_cstr(&ta_str));
                }
#endif
            }
        }

        // try to specialize — resolve AST type args to monotypes
        ast_node_sized    callsite_type_args = {.size = node->named_application.n_type_arguments,
                                                .v    = node->named_application.type_arguments};
        hashmap          *outer_type_args    = traverse_ctx ? traverse_ctx->type_arguments : null;
        tl_monotype_sized resolved_type_args = {
          .size = callsite_type_args.size,
          .v    = callsite_type_args.size
                    ? alloc_malloc(self->transient, callsite_type_args.size * sizeof(tl_monotype *))
                    : null,
        };
        forall(i, callsite_type_args) {
            resolved_type_args.v[i] = parse_type_arg(self, outer_type_args, callsite_type_args.v[i]);
        }
#if DEBUG_RECURSIVE_TYPES
        {
            str arrow_str = tl_monotype_to_string(self->transient, callsite->type);
            fprintf(stderr,
                    "[DEBUG_RECURSIVE_TYPES] specialize_applications_cb: about to specialize_arrow '%s' "
                    "concrete=%d arrow=%s\n",
                    str_cstr(&name), tl_monotype_is_concrete(callsite->type), str_cstr(&arrow_str));
        }
#endif
        if (specialize_arrow_with_name(self, traverse_ctx, node->named_application.name, callsite->type,
                                       resolved_type_args)) {
            dbg_at(2, self, "note: failed to specialize '%s'", str_cstr(&name));
            return 1;
        }
        // and recurse over any arguments which are toplevel functions
        if (specialize_arguments(self, traverse_ctx, node, callsite->type)) {
            dbg_at(2, self, "note: failed to specialize arguments of '%s'", str_cstr(&name));
            return 1;
        }

        // Variadic call: specialize the trait function implementation for each variadic arg.
        // This ensures the specialized function exists for the transpiler to emit trait fn calls.
        if (node->named_application.is_variadic_call) {
            str       func_name = ast_node_str(node->named_application.name);
            u8        n_fixed   = node->named_application.n_fixed_args;
            u32       n_total   = node->named_application.n_arguments;
            u32       n_va      = n_total - n_fixed;

            ast_node *func_let  = toplevel_get(self, func_name);
            if (func_let && ast_node_is_let(func_let) && func_let->let.is_variadic) {
                ast_node *last_param = func_let->let.parameters[func_let->let.n_parameters - 1];
                ast_node *ann        = last_param->symbol.annotation;
                if (ann && ast_node_is_nfa(ann) && ann->named_application.n_type_arguments == 1) {
                    str           trait_name = ast_node_str(ann->named_application.type_arguments[0]);
                    tl_trait_def *trait      = str_map_get_ptr(self->traits, trait_name);
                    if (trait && trait->sigs.size == 1) {
                        tl_trait_sig *sig = &trait->sigs.v[0];

                        // Extract elem_type from Slice param (last param of function's arrow).
                        // Slice[T] = { v: Ptr[T], size: CSize }, so args.v[0] = Ptr[T].
                        tl_polytype *func_poly  = tl_type_env_lookup(self->env, func_name);
                        tl_monotype *func_arrow = func_poly ? func_poly->type : null;
                        tl_monotype *elem_type  = null;
                        tl_monotype *slice_mon  = null;
                        if (func_arrow && tl_monotype_is_arrow(func_arrow)) {
                            tl_monotype      *ptuple = func_arrow->list.xs.v[0];
                            tl_monotype_sized pms    = ptuple->list.xs;
                            slice_mon                = (pms.size > 0) ? pms.v[pms.size - 1] : null;
                            if (slice_mon && tl_monotype_is_inst(slice_mon) &&
                                slice_mon->cons_inst->args.size > 0 &&
                                tl_monotype_is_ptr(slice_mon->cons_inst->args.v[0]))
                                elem_type = tl_monotype_ptr_target(slice_mon->cons_inst->args.v[0]);
                        }

                        if (elem_type) {
                            // Ensure Slice[ElemType] is specialized as a C struct.
                            // Pass the concrete field types from the arrow's Slice instance.
                            specialize_type_constructor(self, S("Slice"), slice_mon->cons_inst->args, null);

                            node->named_application.variadic_impl_fns =
                              alloc_malloc(self->arena, n_va * sizeof(str));
                            node->named_application.variadic_trait_fn = sig->name;

                            // Allocate format dispatch flags if format specs are present.
                            tl_fstring_format *ffmt = node->named_application.fstring_fmt;
                            tl_format_spec *fspecs  = ffmt ? ffmt->specs : null;

                            // Look up FormatSpec type once (used by both per-arg and layout paths).
                            tl_monotype *fs_type = null;
                            if (ffmt) {
                                ffmt->uses_format = alloc_malloc(self->arena, n_va * sizeof(u8));
                                memset(ffmt->uses_format, 0, n_va * sizeof(u8));
                                tl_polytype *fs_poly =
                                  tl_type_env_lookup(self->env, S("FormatSpec__FormatSpec"));
                                fs_type = fs_poly ? fs_poly->type : null;
                            }

                            for (u32 vi = 0; vi < n_va; vi++) {
                                ast_node    *arg      = node->named_application.arguments[n_fixed + vi];
                                tl_monotype *arg_type = arg->type ? arg->type->type : null;
                                if (arg_type && !tl_monotype_is_concrete(arg_type))
                                    tl_monotype_substitute(self->arena, arg_type, self->subs, null);

                                // Check if this argument has a type-specific format spec.
                                int has_fmt_spec = fspecs && fspecs[n_fixed + vi].has_type_specific;

                                str impl = str_empty();
                                int use_format = 0;

                                // Two-phase lookup: try to_string_format first if format spec present.
                                if (has_fmt_spec && arg_type && tl_monotype_is_inst(arg_type)) {
                                    impl = find_overload_func(self, arg_type, "to_string_format", 2);
                                    if (!str_is_empty(impl)) {
                                        use_format = 1;
                                    } else {
                                        // Type-specific spec but no ToStringFormat impl — error.
                                        str type_str = tl_monotype_to_string(self->transient, arg_type);
                                        str msg = str_fmt(self->arena,
                                            "format specifier requires ToStringFormat trait, "
                                            "not implemented for type %s", str_cstr(&type_str));
                                        array_push(self->errors,
                                            ((tl_infer_error){.tag  = tl_err_trait_bound_not_satisfied,
                                                              .node = arg, .message = msg}));
                                    }
                                }

                                // Fall back to regular to_string.
                                if (str_is_empty(impl) && arg_type && tl_monotype_is_inst(arg_type))
                                    impl = find_overload_func(self, arg_type, str_cstr(&sig->name),
                                                              sig->arity);

                                // Build callsite arrow and specialize.
                                if (!str_is_empty(impl)) {
                                    u32 arity = use_format && fs_type ? 2 : 1;
                                    tl_monotype **param_vs =
                                      alloc_malloc(self->arena, arity * sizeof(tl_monotype *));
                                    param_vs[0] = arg_type;
                                    if (arity == 2) param_vs[1] = fs_type;
                                    tl_monotype *ptup = tl_monotype_create_tuple(
                                      self->arena, (tl_monotype_sized){.v = param_vs, .size = arity});
                                    tl_monotype *va_arrow =
                                      tl_type_registry_create_arrow(self->registry, ptup, elem_type);
                                    str spec = specialize_arrow(self, traverse_ctx, impl, va_arrow,
                                                                (tl_monotype_sized){0});
                                    impl = str_is_empty(spec) ? impl : spec;
                                }
                                node->named_application.variadic_impl_fns[vi] = impl;
                                if (ffmt && use_format)
                                    ffmt->uses_format[vi] = 1;
                            }

                            // Pre-specialize FormatSpec.apply_layout if any arg has layout specs.
                            if (fspecs) {
                                int needs_layout = 0;
                                for (u32 vi = 0; vi < n_va && !needs_layout; vi++) {
                                    if (tl_format_spec_has_any(&fspecs[n_fixed + vi]))
                                        needs_layout = 1;
                                }
                                if (needs_layout && fs_type) {
                                    specialize_type_constructor(self, S("FormatSpec"),
                                        (tl_monotype_sized){0}, null);
                                    if (elem_type) {
                                        // Arrow: (String, FormatSpec) -> String
                                        tl_monotype **lp =
                                          alloc_malloc(self->arena, 2 * sizeof(tl_monotype *));
                                        lp[0] = elem_type;
                                        lp[1] = fs_type;
                                        tl_monotype *ptup = tl_monotype_create_tuple(
                                          self->arena, (tl_monotype_sized){.v = lp, .size = 2});
                                        tl_monotype *layout_arrow =
                                          tl_type_registry_create_arrow(self->registry, ptup, elem_type);
                                        str base = S("FormatSpec__apply_layout__2");
                                        str spec = specialize_arrow(self, traverse_ctx, base,
                                                                    layout_arrow, (tl_monotype_sized){0});
                                        ffmt->layout_fn = str_is_empty(spec) ? base : spec;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        dbg(self, "specialize_applications_cb done: nfa '%s'",
            str_cstr(&node->named_application.name->symbol.name));

    } else {
        dbg(self, "specialize_applications_cb: anon");
        callsite = make_arrow(self, traverse_ctx, ast_node_sized_from_ast_array(node), node, 0);

        concretize_params(self, node, callsite->type, null, (tl_monotype_sized){0});
        if (post_specialize(self, traverse_ctx, node->lambda_application.lambda, callsite->type)) {
            return 1;
        }
        if (specialize_arguments(self, traverse_ctx, node, callsite->type)) {
            return 1;
        }
    }

    return 0;
}

// ============================================================================
// Generic function registration (Phase 3)
// ============================================================================

static int generic_declaration(tl_infer *self, str name, ast_node const *name_node, ast_node *node) {
    // no function body, so let's treat this as a type declaration
    if (!name_node->symbol.annotation_type) {
        expected_type(self, node);
        return 1;
    }

    // must quantify arrow types
    if (tl_monotype_is_arrow(node->symbol.annotation_type->type)) {
        tl_polytype_generalize(node->symbol.annotation_type, self->env, self->subs);
    }
    tl_type_env_insert(self->env, name, node->symbol.annotation_type);
    tl_infer_set_attributes(self, node);
    return 0;
}

static int infer_one(tl_infer *self, ast_node *infer_target, tl_polytype *arrow) {
    // arrow is non-null only for let nodes
    if (arrow && !ast_node_is_let(infer_target) && !ast_node_is_lambda_function(infer_target))
        fatal("logic error");

    traverse_ctx *traverse = traverse_ctx_create(self->transient);
    if (self->report_stats) self->counters.traverse_infer_calls++;
    if (traverse_ast(self, traverse, infer_target, infer_traverse_cb)) return 1;

    // constrain arrow result type and infer target's type
    if (arrow) {
        if (tl_polytype_is_scheme(arrow)) fatal("logic error");
        ast_node *body = null;
        if (ast_node_is_let(infer_target)) body = infer_target->let.body;
        else if (ast_node_is_lambda_function(infer_target)) body = infer_target->lambda_function.body;
        if (!body) fatal("logic error");

        tl_polytype wrap_result = tl_polytype_wrap(tl_monotype_arrow_result(arrow->type));
        if (constrain(self, &wrap_result, body->type, body, TL_UNIFY_DIRECTED)) return 1;
    }
    return 0;
}

int add_generic(tl_infer *self, ast_node *node) {
    if (!node) return 0;

    // Handle body nodes early - they contain multiple definitions (e.g., from tagged union desugaring)
    if (ast_node_is_body(node)) {
        forall(i, node->body.expressions) {
            if (add_generic(self, node->body.expressions.v[i])) return 1;
        }
        return 0;
    }

    ast_node    *infer_target = get_infer_target(node);
    ast_node    *name_node    = toplevel_name_node(node);
    tl_polytype *provisional  = name_node->symbol.annotation_type;
    str          name         = name_node->symbol.name;
    str          orig_name    = name_node->symbol.original;

    tl_infer_set_attributes(self, name_node);

    // calculate provisional type, for recursive functions
    if (ast_node_is_let(node)) {
        if (!provisional) {
            // Note: special case: force main() to have a CInt result type
            if (is_main_function(name)) {
                provisional = make_arrow_result_type(self, null, ast_node_sized_from_ast_array(node),
                                                     tl_type_registry_get(self->registry, S("CInt")), 1);
            }

            else {
                provisional =
                  make_arrow(self, null, ast_node_sized_from_ast_array(node), node->let.body, 1);
            }
        }
    } else if (ast_node_is_let_in_lambda(node)) {
        if (!provisional)
            provisional = make_arrow(self, null, ast_node_sized_from_ast_array(infer_target),
                                     node->let_in.value->lambda_function.body, 1);
    } else if (ast_node_is_symbol(node)) {
        // toplevel symbol node, e.g. for declaration of intrinsics, or forward type annotations. They will
        // take precedence to any later declarations, so let's be careful
    } else if (ast_node_is_trait_def(node)) {
        // already loaded from load_toplevel
        return 0;
    } else if (ast_node_is_utd(node)) {
        // already loaded from load_toplevel
        return 0;
    } else if (ast_node_is_let_in(node)) {
        if (infer_one(self, infer_target, null)) {
            dbg_at(2, self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
                   str_ilen(orig_name), str_buf(&orig_name));
        }

        assert(node->let_in.value->type);
        tl_polytype *let_type = node->let_in.name->symbol.annotation_type
                                  ? node->let_in.name->symbol.annotation_type
                                  : node->let_in.value->type;
        tl_type_env_insert(self->env, name, let_type);
        ast_node_type_set(node->let_in.name, let_type);
        return 0;

    } else {
        fatal("logic error");
    }

    dbg(self, "-- add_generic: %.*s (%.*s) --", str_ilen(name), str_buf(&name), str_ilen(orig_name),
        str_buf(&orig_name));

    if (!infer_target) {
        // no function body, so let's treat this as a type declaration
        return generic_declaration(self, name, name_node, node);
    }

    // ensure provisional type is not quantified. If it is, instantiate it
    if (provisional && tl_polytype_is_scheme(provisional)) {
        provisional = tl_polytype_absorb_mono(
          self->arena, tl_polytype_instantiate(self->arena, provisional, self->subs));
    }

    // add provisional type to environment (for polymorphic recursion)
    if (provisional) {
        // Note: ensure this is not quantified until after inference
        tl_type_env_insert(self->env, name, provisional);
    }

    // run inference
    if (infer_one(self, infer_target, provisional)) {
        dbg_at(2, self, "-- add_generic error: %.*s (%.*s) --", str_ilen(name), str_buf(&name),
               str_ilen(orig_name), str_buf(&orig_name));
        return 1;
    }

    // Must apply subs before quantifying, because we want to replace any tvs (that would otherwise be
    // quantified) with primitives if possible, or the same root of an equivalence class
    tl_type_subs_apply(self->subs, self->env);

    // get the arrow type from the annotation, or else from the result of inference
    tl_polytype *arrow = null;

    // get the arrow type from inference, or else from the annotation, if any
    arrow = tl_type_env_lookup(self->env, name);
    if (!arrow) arrow = name_node->symbol.annotation_type;
    if (!arrow) fatal("runtime error");

    tl_polytype_generalize(arrow, self->env, self->subs);

    // collect free variables from infer target and add to the generic's arrow type

    add_free_variables_to_arrow(self, infer_target, arrow);
    tl_type_env_insert(self->env, name, arrow);

    {
        str tmp = tl_polytype_to_string(self->transient, arrow);
        dbg(self, "-- done add_generic: %.*s (%.*s): type : %s --", str_ilen(name), str_buf(&name),
            str_ilen(orig_name), str_buf(&orig_name), str_cstr(&tmp));
        str_deinit(self->transient, &tmp);
    }

    return 0;
}

// ============================================================================
// Post-inference validation and cleanup
// ============================================================================

typedef struct {
    str       target;
    ast_node *found;
    tl_infer *self;
    hashmap  *visited_fns;
} find_symbol_ctx;

static void find_symbol_cb(void *ctx_, ast_node const *node) {
    find_symbol_ctx *ctx = ctx_;
    if (ctx->found) return;

    if (ast_node_is_symbol(node) && 0 == str_cmp(ast_node_str(node), ctx->target)) {
        ctx->found = (ast_node *)node;
        return;
    }

    if (ast_node_is_nfa(node)) {
        str callee_name = ast_node_str(node->named_application.name);
        if (!str_hset_contains(ctx->visited_fns, callee_name)) {
            str_hset_insert(&ctx->visited_fns, callee_name);
            ast_node *callee = toplevel_get(ctx->self, callee_name);
            if (callee && !ctx->found) {
                ast_node_cdfs(ctx, callee, (ast_op_cfun)find_symbol_cb);
            }
        }
    }
}

typedef struct {
    tl_infer *self;
    hashmap  *reported;
} missing_fv_ctx;

static void missing_fv_error_cb(void *ctx_, str fun, str var) {
    missing_fv_ctx *ctx = ctx_;
    if (str_hset_contains(ctx->reported, var)) return;
    str_hset_insert(&ctx->reported, var);

    ast_node       *node = toplevel_get(ctx->self, fun);

    find_symbol_ctx fctx = {
      .target      = var,
      .found       = NULL,
      .self        = ctx->self,
      .visited_fns = hset_create(ctx->self->transient, 64),
    };
    if (node) ast_node_cdfs(&fctx, node, (ast_op_cfun)find_symbol_cb);

    ast_node *err_node = fctx.found ? fctx.found : node;
    array_push(ctx->self->errors,
               ((tl_infer_error){.tag = tl_err_free_variable_not_found, .node = err_node, .message = var}));
}

int check_missing_free_variables(tl_infer *self) {
    missing_fv_ctx ctx = {
      .self     = self,
      .reported = hset_create(self->transient, 64),
    };
    return tl_type_env_check_missing_fvs(self->env, missing_fv_error_cb, &ctx);
}

void remove_generic_toplevels(tl_infer *self) {
    str_array        names = {.alloc = self->transient};

    ast_node        *node;
    hashmap_iterator iter = {0};
    while ((node = toplevel_iter(self, &iter))) {
        if (ast_node_is_utd(node)) continue; // preserve type definitions as specialization templates

        str name = ast_node_str(toplevel_name_node(node));
        if (is_main_function(name)) continue;

        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");

        if (!tl_polytype_is_concrete(type)) array_push(names, name);
    }

    forall(i, names) {
        dbg(self, "remove_generic_toplevels: removing '%s'", str_cstr(&names.v[i]));
        toplevel_del(self, names.v[i]);
    }
    array_free(names);
}

int check_main_function(tl_infer *self, ast_node *main) {
    // instantiate and infer main
    assert(ast_node_is_let(main));
    tl_polytype *type = tl_type_env_lookup(self->env, S("main"));
    if (!type) fatal("main function with no type");

    // set is_specialized
    ast_node_set_is_specialized(main);

    tl_polytype *body_type = main->let.body->type;
    if (!body_type || tl_polytype_is_scheme(body_type)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_main_function_bad_type, .node = main}));
        return 1;
    }

    // remove free variables from main type if they are toplevel (e.g. lambda functions)
    str_sized *fvs = &type->type->list.fvs;
    if (tl_monotype_is_arrow(type->type)) {
        for (u32 i = 0; i < fvs->size;) {
            str fv = fvs->v[i];
            if (toplevel_get(self, fv)) array_sized_erase(*fvs, i);
            else ++i;
        }
    }

    // report errors: main must have no free variables
    int error = 0;
    forall(i, *fvs) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_unknown_symbol_in_main, .message = fvs->v[i]}));
        ++error;
    }
    return error;
}
