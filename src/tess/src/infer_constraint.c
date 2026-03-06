// infer_constraint.c — Phases 2-4: Loading, Inference & Constraint Solving
//
// Phase 2: load_toplevel scans all top-level definitions, registering types and
// creating type-constructor entries.
// Phase 3: add_generic traverses each function body bottom-up via
// infer_traverse_cb, generating type constraints.
// Phase 4: check_missing_free_variables verifies no unresolved free variables.
//
// Also contains: traverse_ast (the main AST walker), resolve_node, all the
// infer_* type inference handlers, constraint functions, and error reporting.

#include "infer_internal.h"

// ============================================================================
// Constraint and error helpers
// ============================================================================

int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node,
              tl_unify_direction dir);

int env_insert_constrain(tl_infer *self, str name, tl_polytype *type, ast_node const *name_node) {

    // insert type in the environment, but constrains against existing type, if any.

    tl_polytype *exist = tl_type_env_lookup(self->env, name);
    if (exist) {
        if (constrain(self, exist, type, name_node, TL_UNIFY_SYMMETRIC)) return 1;
    } else {
        tl_type_env_insert(self->env, name, type);
        tl_infer_set_attributes(self, name_node);
    }
    return 0;
}

void expected_type(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_type, .node = node}));
}

void expected_tagged_union(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_expected_tagged_union, .node = node}));
}

void wrong_number_of_arguments(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_arity, .node = node}));
}

void tagged_union_case_syntax_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors,
               ((tl_infer_error){.tag = tl_err_tagged_union_case_syntax_error, .node = node}));
}

static void create_type_constructor_from_user_type(tl_infer *self, ast_node *node) {
    assert(ast_node_is_utd(node));

    tl_type_registry_parse_type_ctx_reset(&self->type_parse_ctx);

    str tu_name = node->user_type_def.tagged_union_name;
    if (!str_is_empty(tu_name)) {
        str_hset_insert(&self->type_parse_ctx.in_progress, tu_name);
    }

    tl_monotype *mono = tl_type_registry_parse_type_with_ctx(self->registry, node, &self->type_parse_ctx);
    if (!mono) {
        expected_type(self, node);
        return;
    }

    str name = node->user_type_def.name->symbol.name;
    tl_type_registry_insert_mono(self->registry, name, mono);
    tl_polytype *poly = tl_type_registry_get(self->registry, name);

    env_insert_constrain(self, name, poly, node->user_type_def.name);
    ast_node_type_set(node, poly);

    // Auto-collapse: if type is Module__Module (e.g. Array__Array), register
    // bare module name as alias so clients can write Array[T] instead of Array.Array[T].
    // Also store the module name in the type constructor def for operator overload dispatch.
    ast_node *type_name_node = node->user_type_def.name;
    if (tl_monotype_is_inst(poly->type) && ast_node_is_symbol(type_name_node)) {
        poly->type->cons_inst->def->module = type_name_node->symbol.module;
    }
    if (ast_node_is_symbol(type_name_node) && type_name_node->symbol.is_mangled) {
        str module   = type_name_node->symbol.module;
        str original = type_name_node->symbol.original;
        if (!str_is_empty(module) && str_eq(original, module)) {
            if (!tl_type_registry_get(self->registry, module)) {
                tl_type_registry_type_alias_insert(self->registry, module, poly);
            }
        }
    }
}

// ============================================================================
// Top-level loading (Phase 2)
// ============================================================================

static int toplevel_hash_command(tl_infer *self, ast_node *node) {
    assert(ast_node_is_hash_command(node));

    // skip #ifc .. #endc blocks
    if (ast_node_is_ifc_block(node)) return 0;

    str_sized words = node->hash_command.words;

    if (words.size < 2) {
        wrong_number_of_arguments(self, node);
        return 1;
    }

    if (str_eq(words.v[0], S("include"))) {
        array_push(self->hash_includes, words.v[1]);
        return 0;
    } else if (str_eq(words.v[0], S("import"))) {
        return 0;
    } else if (str_eq(words.v[0], S("unity_file"))) {
        return 0;
    } else if (str_eq(words.v[0], S("module"))) {
        return 0;
    } else if (str_eq(words.v[0], S("module_prelude"))) {
        return 0;
    } else if (str_eq(words.v[0], S("alias"))) {
        return 0;
    } else if (str_eq(words.v[0], S("unalias"))) {
        return 0;
    } else {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_unknown_hash_command, .node = node}));
        return 1;
    }
}

// Handle a let node during toplevel loading: merge forward declarations, copy annotations/attributes.
static void load_toplevel_let(tl_infer *self, ast_node *node) {
    str        name_str = ast_node_str(node->let.name);
    ast_node **p        = str_map_get(self->toplevels, name_str);

    if (p) {
        // merge type if the existing node is a symbol; otherwise error
        if (!ast_node_is_symbol(*p)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            return;
        }

        // ignore prior type annotation if the current symbol is annotated: later
        // declaration overrides

        if (node->let.name->symbol.annotation) {
            resolve_node(self, node->let.name, null, npos_toplevel);
        } else {
            // otherwise merge in the prior annotation
            node->let.name->symbol.annotation      = (*p)->symbol.annotation;
            node->let.name->symbol.annotation_type = (*p)->symbol.annotation_type;
        }

        // copy attributes over
        if ((*p)->symbol.attributes) {
            // reject attributes on current symbol if they exist
            if (node->let.name->symbol.attributes) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_attributes_exist, .node = node}));
                return;
            }

            node->let.name->symbol.attributes = (*p)->symbol.attributes;
        }

        // copy parameter annotations over
        if ((*p)->symbol.annotation) {
            // The annotation is an AST arrow, which includes param annotations, if any. These are
            // important to copy over, because they may declare type arguments.
            ast_node *ast_arrow = (*p)->symbol.annotation;
            assert(ast_node_is_arrow(ast_arrow));
            ast_node *ast_param_tuple = ast_arrow->arrow.left;
            assert(ast_node_is_tuple(ast_param_tuple));

            // copy explicit type arguments if the current node does not declare any
            if (!node->let.n_type_parameters) {
                node->let.n_type_parameters = ast_arrow->arrow.n_type_parameters;
                node->let.type_parameters   = ast_arrow->arrow.type_parameters;
            }

            tl_polytype *arrow = (*p)->symbol.annotation_type;
            assert(arrow && tl_monotype_is_arrow(arrow->type));
            tl_monotype *param_tuple = arrow->type->list.xs.v[0];
            assert(tl_tuple == param_tuple->tag);
            ast_arguments_iter iter = ast_node_arguments_iter(node);
            ast_node          *arg;
            u32                j = 0;
            while ((arg = ast_arguments_next(&iter))) {
                if (j >= param_tuple->list.xs.size) fatal("runtime error");
                if (j >= ast_param_tuple->tuple.n_elements) fatal("runtime error");

                if (!ast_node_is_symbol(arg)) goto next;

                // Do not overwrite let node's annotated parameters
                if (arg->symbol.annotation) goto next;

                arg->symbol.annotation = ast_param_tuple->tuple.elements[j];
                arg->symbol.annotation_type =
                  tl_polytype_absorb_mono(self->arena, param_tuple->list.xs.v[j]);

            next:
                j++;
            }
        }

        // replace prior symbol entry with let node
        *p = node;
    } else {
        str_map_set(&self->toplevels, name_str, &node);
        resolve_node(self, node->let.name, null, npos_toplevel);
    }
}

void load_toplevel(tl_infer *self, ast_node_sized nodes) {
    // Types of toplevel nodes (see parser.c/toplevel())
    //
    // - struct/union type definition (utd, user_type_def)
    // - enum type definition (utd)
    // - function definition (let node)
    // - global value definition (let-in node)
    // - forward function declaration `(p1, p2, ...) -> r` (symbol with an arrow annotation)
    // - symbol annotation `sym : Type`
    // - type alias
    // - c chunks and hash directives, not processed here
    //
    // If the same symbol is seen more than once, it is usually an error. The exception is forward function
    // declarations.

    forall(i, nodes) {
        ast_node *node = nodes.v[i];
        if (ast_node_is_symbol(node)) {
            str        name_str = node->symbol.name;
            ast_node **p        = str_map_get(self->toplevels, name_str);
            if (p) {
                // merge annotation if existing node is a let node; otherwise error
                if (!ast_node_is_let(*p)) {
                    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
                    continue;
                }

                if (node->symbol.annotation) {
                    (*p)->let.name->symbol.annotation = node->symbol.annotation;
                    resolve_node(self, (*p)->let.name, null, npos_toplevel);
                }
            } else {
                // don't bother saving top level unannotated symbol node.
                if (node->symbol.annotation) {
                    str_map_set(&self->toplevels, name_str, &node);
                    resolve_node(self, node, null, npos_toplevel);
                }
            }
        }

        else if (ast_node_is_type_alias(node)) {
            // Alias name may be a symbol or an nfa (from the parser). Only symbols are valid.
            if (!ast_node_is_symbol(node->type_alias.name)) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_expected_type_alias_symbol, .node = node}));
                continue;
            }

            str          name = toplevel_name(node);
            tl_monotype *mono = tl_type_registry_parse_type(self->registry, node->type_alias.target);
            tl_polytype *poly = tl_monotype_generalize(self->arena, mono);
            {
                str poly_str = tl_polytype_to_string(self->transient, poly);
                dbg(self, "type_alias: %s = %s", str_cstr(&name), str_cstr(&poly_str));
            }
            tl_type_registry_type_alias_insert(self->registry, name, poly);
#if DEBUG_TYPE_ALIAS
            {
                tl_polytype *env_type = tl_type_env_lookup(self->env, name);
                str          poly_dbg = tl_polytype_to_string(self->transient, poly);
                fprintf(stderr, "[DEBUG_TYPE_ALIAS] load_toplevel: alias '%s' = %s\n", str_cstr(&name),
                        str_cstr(&poly_dbg));
                fprintf(stderr, "[DEBUG_TYPE_ALIAS]   in registry=YES, in env=%s\n",
                        env_type ? "YES" : "NO");
            }
#endif
            specialize_type_alias(self, node);

            // Insert the (possibly specialized) alias type into the type environment so that
            // symbol lookups (e.g. in infer_struct_access) can resolve alias names to their
            // underlying type constructor — mirroring what create_type_constructor_from_user_type
            // does for enums/structs.
            tl_polytype *final_poly = tl_type_registry_get(self->registry, name);
            if (final_poly) env_insert_constrain(self, name, final_poly, node->type_alias.name);
        }

        else if (ast_node_is_let(node)) {
            load_toplevel_let(self, node);
        }

        else if (ast_node_is_trait_def(node)) {
            str name_str = ast_node_str(node->trait_def.name);

            // Check for duplicate type/trait name
            if (str_map_get(self->toplevels, name_str) || str_map_get_ptr(self->traits, name_str)) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                // Register trait in the trait registry
                tl_trait_def *def = new(self->arena, tl_trait_def);
                def->name         = str_copy(self->arena, name_str);
                def->generic_name = str_copy(self->arena, node->trait_def.name->symbol.original);
                def->parents      = (str_array){.alloc = self->arena};
                def->sigs         = (tl_trait_sig_array){.alloc = self->arena};

                // Collect parent trait names
                for (u32 i = 0; i < node->trait_def.n_parents; i++) {
                    ast_node *parent = node->trait_def.parents[i];
                    str raw_name = ast_node_is_nfa(parent)
                                     ? ast_node_str(parent->named_application.name)
                                     : ast_node_str(parent);
                    str parent_name = str_copy(self->arena, raw_name);
                    array_push(def->parents, parent_name);
                }

                // Collect signatures (name + arity)
                for (u32 i = 0; i < node->trait_def.n_signatures; i++) {
                    ast_node *sig = node->trait_def.signatures[i];
                    u8  arity     = 0;
                    if (sig->symbol.annotation && ast_node_is_arrow(sig->symbol.annotation)) {
                        ast_node_sized params =
                          ast_node_sized_from_ast_array_const(sig->symbol.annotation->arrow.left);
                        arity = (u8)params.size;
                    }
                    tl_trait_sig tsig = {.name = str_copy(self->arena, ast_node_str(sig)), .arity = arity};
                    array_push(def->sigs, tsig);
                }

                str_map_set_ptr(&self->traits, name_str, def);
            }
        }

        else if (ast_node_is_utd(node)) {
            str        name_str = ast_node_str(node->user_type_def.name);
            ast_node **p        = str_map_get(self->toplevels, name_str);

            if (p || str_map_get_ptr(self->traits, name_str)) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_exists, .node = node}));
            } else {
                create_type_constructor_from_user_type(self, node);
                str_map_set(&self->toplevels, name_str, &node);
#if DEBUG_EXPLICIT_TYPE_ARGS
                fprintf(stderr, "[DEBUG UTD] Added to toplevels (%p): '%s' (len=%zu, hash=%llu) -> %p\n",
                        (void *)self->toplevels, str_cstr(&name_str), str_len(name_str),
                        (unsigned long long)str_hash64(name_str), (void *)node);
#endif
            }
        }

        else if (ast_node_is_let_in(node)) {
            str name_str = node->let_in.name->symbol.name;
            str_map_set(&self->toplevels, name_str, &node);
            resolve_node(self, node->let_in.name, null, npos_toplevel);
        }

        else if (ast_node_is_hash_command(node)) {
            (void)toplevel_hash_command(self, node);
        }

        else if (ast_node_is_body(node)) {
            load_toplevel(self, node->body.expressions);
        }

        else {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_invalid_toplevel, .node = node}));
            continue;
        }
    }

}

// ============================================================================
// Traverse context and AST traversal
// ============================================================================

traverse_ctx *traverse_ctx_create(allocator *transient) {
    // Use a transient allocator because the destroy function leaks the maps.
    traverse_ctx *out   = new(transient, traverse_ctx);
    out->lexical_names  = hset_create(transient, 64);
    out->type_arguments = map_create_ptr(transient, 64);
    out->user           = null;
    out->result_type    = null;
    out->node_pos       = npos_operand;
    out->is_field_name  = 0;
    out->is_annotation  = 0;

    return out;
}

void traverse_ctx_load_type_arguments(tl_infer *self, traverse_ctx *ctx, ast_node const *node) {
    // read type arguments out of ast node
    if (ast_node_is_let(node)) {
        for (u32 i = 0; i < node->let.n_type_parameters; i++) {
            ast_node *type_param = node->let.type_parameters[i];
            assert(ast_node_is_symbol(type_param));

#if DEBUG_INVARIANTS
            // Invariant: No duplicate type parameter names in ctx->type_arguments
            if (str_map_contains(ctx->type_arguments, type_param->symbol.name)) {
                char detail[256];
                snprintf(detail, sizeof detail, "Duplicate type parameter name '%.*s'",
                         str_ilen(type_param->symbol.name), str_buf(&type_param->symbol.name));
                report_invariant_failure(self, "traverse_ctx_load_type_arguments",
                                         "No duplicate type parameter names allowed", detail, type_param);
            }
#endif

            // If the type parameter already has a type (set by concretize_params during
            // specialization, or by a previous traversal), use that instead of creating a fresh
            // type variable.
            if (type_param->type) {
                tl_monotype *mono = type_param->type->type;
                if (!tl_monotype_is_concrete(mono))
                    tl_monotype_substitute(self->arena, mono, self->subs, null);
#if DEBUG_EXPLICIT_TYPE_ARGS
                str mono_str = tl_monotype_to_string(self->transient, mono);
                fprintf(stderr, "[DEBUG LOAD TYPE ARGS] '%s' type_param '%s' has existing type: %s\n",
                        str_cstr(&node->let.name->symbol.name), str_cstr(&type_param->symbol.name),
                        str_cstr(&mono_str));
#endif

                str param_name = type_param->symbol.name;

                tl_type_registry_add_type_argument(self->registry, param_name, mono, &ctx->type_arguments);
                assert(str_map_contains(ctx->type_arguments, param_name));

                // param names are alpha converted, and we use the environment to ensure constraints are
                // fully propagated
                tl_polytype *poly = tl_polytype_absorb_mono(self->arena, mono);
                env_insert_constrain(self, param_name, poly, type_param);

            } else {

#if DEBUG_EXPLICIT_TYPE_ARGS
                fprintf(
                  stderr,
                  "[DEBUG LOAD TYPE ARGS] '%s' type_param '%s' has NO existing type, creating fresh\n",
                  str_cstr(&node->let.name->symbol.name), str_cstr(&type_param->symbol.name));
#endif
                tl_monotype *mono = tl_type_registry_add_fresh_type_argument(
                  self->registry, type_param->symbol.name, &ctx->type_arguments);

                ast_node_type_set(type_param, tl_polytype_absorb_mono(self->arena, mono));

                assert(str_map_contains(ctx->type_arguments, type_param->symbol.name));
            }
        }
    }
}

int traverse_ctx_assign_type_arguments(tl_infer *self, traverse_ctx *ctx, ast_node const *node) {
    if (ast_node_is_nfa(node)) {
        if (!node->named_application.is_specialized
            && !node->named_application.is_function_reference
            && !node->named_application.is_type_constructor)
            return 0;

        u32 argc = node->named_application.n_type_arguments;
        if (argc == 0) return 0;
        ast_node **argv   = node->named_application.type_arguments;

        ast_node  *let    = toplevel_get(self, ast_node_str(node->named_application.name));
        u32        paramc = (let && ast_node_is_let(let)) ? let->let.n_type_parameters : 0;

#if DEBUG_INVARIANTS
        // Invariant: Type argument count must match type parameter count
        if (argc > 0 && paramc > 0 && argc != paramc) {
            str  callee_name = ast_node_str(node->named_application.name);
            char detail[256];
            snprintf(detail, sizeof detail,
                     "Call site has %u type arguments but function '%.*s' has %u type parameters", argc,
                     str_ilen(callee_name), str_buf(&callee_name), paramc);
            report_invariant_failure(self, "traverse_ctx_assign_type_arguments",
                                     "Type argument count must match type parameter count", detail,
                                     (ast_node *)node);
        }
#endif

#if DEBUG_EXPLICIT_TYPE_ARGS
        str name = ast_node_str(node->named_application.name);
        fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] traverse_ctx_assign_type_arguments:\n");
        fprintf(stderr, "  callee: %s\n", str_cstr(&name));
        fprintf(stderr, "  n_type_arguments: %u, n_type_parameters: %u\n", argc, paramc);
        fprintf(stderr, "  type_arguments contains: %i\n", str_map_contains(ctx->type_arguments, name));
#endif

        for (u32 i = 0; i < argc; i++) {
            ast_node *type_arg_node = argv[i];

#if DEBUG_EXPLICIT_TYPE_ARGS
            fprintf(stderr, "  type_arg[%u] AST tag=%d", i, type_arg_node->tag);
            if (ast_node_is_symbol(type_arg_node)) {
                str n = type_arg_node->symbol.name;
                fprintf(stderr, " name='%s'", str_cstr(&n));
                fprintf(stderr, " type_arguments contains: %i", str_map_contains(ctx->type_arguments, n));

            } else if (ast_node_is_nfa(type_arg_node)) {
                str n = ast_node_str(type_arg_node->named_application.name);
                fprintf(stderr, " nfa='%s' n_type_args=%u", str_cstr(&n),
                        type_arg_node->named_application.n_type_arguments);
            }
            if (type_arg_node->type) {
                str t = tl_polytype_to_string(self->transient, type_arg_node->type);
                fprintf(stderr, " type=%s", str_cstr(&t));
            }
            fprintf(stderr, "\n");
#endif

            tl_monotype *parsed = null;

            // If the type argument is a symbol bound in the current context's type_arguments
            // (set by concretize_params), prefer that binding over any type on the node.
            // The node may carry a stale type variable from the add_free_variables_to_arrow
            // traversal that runs before concretize_params.
            if (ast_node_is_symbol(type_arg_node)) {
                tl_monotype *from_ctx = str_map_get_ptr(ctx->type_arguments, type_arg_node->symbol.name);
                if (from_ctx) {
                    parsed = from_ctx;
                }
            }

            if (!parsed) {
                parsed = parse_type_arg(self, ctx->type_arguments, type_arg_node);
            }

            if (!parsed) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_expected_type, .node = type_arg_node}));
                return 1;
            }

#if DEBUG_EXPLICIT_TYPE_ARGS
            str parsed_str = tl_monotype_to_string(self->transient, parsed);
            fprintf(stderr, "  type_arg[%u]: parsed = %s\n", i, str_cstr(&parsed_str));
#endif

            // If the type argument is a type constructor instance with arguments, specialize it.
            // This is an exception to the normal design where specialization happens in
            // specialize_applications_cb. We must do it here because intrinsics (like
            // _tl_sizeof_) are skipped by specialize_applications_cb, so their explicit
            // type arguments would never be specialized otherwise.
            if (tl_monotype_is_inst(parsed) && parsed->cons_inst->args.size > 0) {
                tl_polytype *specialized = null;
                (void)specialize_type_constructor(self, parsed->cons_inst->def->generic_name,
                                                  parsed->cons_inst->args, &specialized);
                if (specialized && tl_monotype_is_inst_specialized(specialized->type)) {
                    parsed = specialized->type;
                }
            }

            // If the callee has a matching type parameter, add to the type argument context
            if (i < paramc) {
                assert(ast_node_is_symbol(let->let.type_parameters[i]));
                // Always use the alpha-converted name, not the original, because the type
                // environment relies on alpha conversion to prevent pollution between generic
                // and specialized phases.
                str param_name = let->let.type_parameters[i]->symbol.name;

#if DEBUG_EXPLICIT_TYPE_ARGS
                str parsed_str = tl_monotype_to_string(self->transient, parsed);
                fprintf(stderr, "  mapping type param '%s' -> %s\n", str_cstr(&param_name),
                        str_cstr(&parsed_str));
#endif

#if DEBUG_INVARIANTS
                // Invariant: If type parameter already has a binding, it must be the same type
                // Type pollution occurs when the same alpha-converted name gets different types
                // in different specialization contexts
                tl_monotype *existing_binding = str_map_get_ptr(ctx->type_arguments, param_name);
                if (existing_binding &&
                    tl_monotype_hash64(existing_binding) != tl_monotype_hash64(parsed)) {
                    char detail[512];
                    str  existing_str = tl_monotype_to_string(self->transient, existing_binding);
                    str  new_str      = tl_monotype_to_string(self->transient, parsed);
                    snprintf(detail, sizeof detail,
                             "Type parameter '%.*s' already bound to '%s', cannot rebind to '%s'",
                             str_ilen(param_name), str_buf(&param_name), str_cstr(&existing_str),
                             str_cstr(&new_str));
                    report_invariant_failure(self, "traverse_ctx_assign_type_arguments",
                                             "Type parameter binding conflict (type pollution)", detail,
                                             (ast_node *)node);
                }
#endif

                tl_type_registry_add_type_argument(self->registry, param_name, parsed,
                                                   &ctx->type_arguments);

                assert(str_map_contains(ctx->type_arguments, param_name));
            }

            // Set type on the type argument AST node for the transpiler.
            ast_node_type_set((ast_node *)node->named_application.type_arguments[i],
                              tl_polytype_absorb_mono(self->arena, parsed));
        }
    }

    return 0;
}

int traverse_ctx_is_param(traverse_ctx *self, str name) {
    return str_hset_contains(self->lexical_names, name);
}

// ============================================================================
// Constraint solving
// ============================================================================

int type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_type_error, .node = node}));
    return 1;
}
int unresolved_type_error(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_unresolved_type, .node = node}));
    return 1;
}

// ============================================================================
// Constraint generation and unification
// ============================================================================
//
// constrain() / constrain_mono() are the core constraint engine. They call into
// the substitution-based unifier (tl_type_subs_unify_mono in type.c) and report
// type errors on failure.

static int is_std_function(ast_node *);

typedef struct {
    tl_infer       *self;
    ast_node const *node;
} type_error_cb_ctx;

static void type_error_cb(void *ctx_, tl_monotype *left, tl_monotype *right) {
    type_error_cb_ctx *ctx = ctx_;
    if (!ctx->self->is_constrain_ignore_error) {
        log_type_error_mm(ctx->self, left, right, ctx->node);
        type_error(ctx->self, ctx->node);
    }
}

int constrain_mono(tl_infer *self, tl_monotype *left, tl_monotype *right, ast_node const *node,
                   tl_unify_direction dir) {
    type_error_cb_ctx error_ctx = {.self = self, .node = node};

#if DEBUG_CONSTRAIN
    {
        str left_str  = tl_monotype_to_string(self->transient, left);
        str right_str = tl_monotype_to_string(self->transient, right);
        fprintf(stderr, "[DEBUG CONSTRAIN] constrain_mono: %s <=> %s  (at %s:%d)\n", str_cstr(&left_str),
                str_cstr(&right_str), node->file, node->line);
    }
#endif

#if DEBUG_INVARIANTS
    // Invariant: should never constrain two different concrete cons_inst types directly
    // (excluding integer-compatible pairs). If this fires, a type variable was already
    // bound to the wrong type upstream. Exception: for type predicates, a constrain failure is not an
    // error (is_constrain_ignore_error is set during check_type_predicate).
    if (!self->is_constrain_ignore_error &&
        tl_monotype_is_inst(left) && tl_monotype_is_concrete(left) && tl_monotype_is_inst(right) &&
        tl_monotype_is_concrete(right) && !tl_monotype_is_integer_convertible(left) &&
        !tl_monotype_is_integer_convertible(right)) {
        if (!str_eq(left->cons_inst->def->name, right->cons_inst->def->name) &&
            !str_eq(left->cons_inst->def->generic_name, right->cons_inst->def->generic_name)) {
            char detail[512];
            str  ls = tl_monotype_to_string(self->transient, left);
            str  rs = tl_monotype_to_string(self->transient, right);
            snprintf(detail, sizeof detail,
                     "Constraining two different concrete type constructors: %s vs %s (left@%p right@%p)",
                     str_cstr(&ls), str_cstr(&rs), (void *)left, (void *)right);
            report_invariant_failure(
              self, "constrain_mono",
              "Should never constrain two different concrete type constructors (unless in type predicate)",
              detail, node);
        }
    }
#endif

    hashmap *seen = hset_create(self->transient, 64);
    int      res  = tl_type_subs_unify_mono(self->subs, left, right, type_error_cb, &error_ctx, &seen, dir);

#if DEBUG_CONSTRAIN
    if (res) {
        fprintf(stderr, "[DEBUG CONSTRAIN] constrain_mono: UNIFICATION FAILED\n");
    }
#endif

    return res;
}

int constrain(tl_infer *self, tl_polytype *left, tl_polytype *right, ast_node const *node,
              tl_unify_direction dir) {
    if (self->report_stats) self->counters.unify_calls++;
    if (left == right) return 0;

    hires_timer ct;
    if (self->report_stats) {
        hires_timer_init(&ct);
        hires_timer_start(&ct);
    }

    tl_monotype *lhs = null, *rhs = null;

    if (left->quantifiers.size) lhs = tl_polytype_instantiate(self->arena, left, self->subs);
    else lhs = left->type;
    if (right->quantifiers.size) rhs = tl_polytype_instantiate(self->arena, right, self->subs);
    else rhs = right->type;

    int res = constrain_mono(self, lhs, rhs, node, dir);

    if (self->report_stats) {
        hires_timer_stop(&ct);
        self->counters.unify_ms += hires_timer_elapsed_sec(&ct) * 1000.0;
    }
    return res;
}

int constrain_pm(tl_infer *self, tl_polytype *left, tl_monotype *right, ast_node const *node,
                 tl_unify_direction dir) {
    tl_polytype wrap = tl_polytype_wrap(right);
    return constrain(self, left, &wrap, node, dir);
}

void ensure_tv(tl_infer *self, tl_polytype **type) {
    if (!type) return;
    if (*type) return;
    *type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

// ============================================================================
// Type inference helpers
// ============================================================================

static int infer_literal_type(tl_infer *self, ast_node *node,
                              tl_monotype *(*get_type)(tl_type_registry *)) {
    tl_monotype *ty = get_type(self->registry);
    ensure_tv(self, &node->type);
#if DEBUG_EXPLICIT_TYPE_ARGS
    {
        str ty_str        = tl_monotype_to_string(self->transient, ty);
        str node_type_str = node->type ? tl_polytype_to_string(self->transient, node->type) : str_empty();
        fprintf(stderr, "[DEBUG LITERAL] infer_literal_type at %s:%d:\n", node->file, node->line);
        fprintf(stderr, "  literal type: %s\n", str_cstr(&ty_str));
        fprintf(stderr, "  node->type before constrain: %s\n",
                str_is_empty(node_type_str) ? "(null)" : str_cstr(&node_type_str));
    }
#endif
    return constrain_pm(self, node->type, ty, node, TL_UNIFY_SYMMETRIC);
}

static int infer_weak_float_literal(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *weak = tl_monotype_create_fresh_weak_float(self->subs);
    return constrain_pm(self, node->type, weak, node, TL_UNIFY_SYMMETRIC);
}

static int infer_weak_int_literal(tl_infer *self, ast_node *node, int is_signed) {
    ensure_tv(self, &node->type);
    tl_monotype *weak = is_signed ? tl_monotype_create_fresh_weak_int_signed(self->subs)
                                  : tl_monotype_create_fresh_weak_int_unsigned(self->subs);
    // Store the literal's numeric value for compile-time range checking.
    i64 val = is_signed ? ast_node_i64(node)->val : (i64)ast_node_u64(node)->val;
    tl_type_subs_set_literal_value(self->subs, weak->var, val);
    return constrain_pm(self, node->type, weak, node, TL_UNIFY_SYMMETRIC);
}

annotation_parse_result parse_type_annotation(tl_infer *self, traverse_ctx *ctx,
                                              ast_node *annotation_node) {

    if (!ast_node_is_symbol(annotation_node) && !ast_node_is_nfa(annotation_node))
        return (annotation_parse_result){0};

    tl_type_registry_parse_type_ctx parse_ctx;
    tl_monotype                    *parsed = tl_type_registry_parse_type_out_ctx(
      self->registry, annotation_node, self->transient, ctx ? ctx->type_arguments : null, &parse_ctx);

    return (annotation_parse_result){.parsed = parsed, .type_arguments = parse_ctx.type_arguments};
}

static int infer_struct_access(tl_infer *, traverse_ctx *, ast_node *);

// Returns: 0 = no annotation, 1 = annotation processed, -1 = error

// ============================================================================
// Inference handlers
// ============================================================================

int process_annotation(tl_infer *self, traverse_ctx *ctx, ast_node *node, annotation_opts opts) {
    if (!node) return 0;

    // parse_type_annotation knows how to look a symbol node's annotation
    annotation_parse_result result = parse_type_annotation(self, ctx, node);

    if (!result.parsed) return 0;

    tl_monotype *mono = result.parsed;

    // Handle type argument self-reference (for formal parameters)
    if (opts.check_type_arg_self && ast_node_is_symbol(node)) {
        str          name  = ast_node_str(node);
        tl_monotype *found = str_map_get_ptr(result.type_arguments, name);
        if (found) mono = found;
    }

    // Set annotation_type field of symbol nodes
    if (ast_node_is_symbol(node)) {
        node->symbol.annotation_type = tl_polytype_absorb_mono(self->arena, mono);
        assert(node->symbol.annotation_type);
    }

    // Constrain node type
    tl_polytype *poly =
      ast_node_is_symbol(node) ? node->symbol.annotation_type : tl_polytype_absorb_mono(self->arena, mono);

#if DEBUG_RESOLVE
    str node_str = v2_ast_node_to_string(self->transient, node);
    str mono_str = tl_monotype_to_string(self->transient, mono);
    fprintf(stderr, "process_annotation %s : %s\n", str_cstr(&node_str), str_cstr(&mono_str));
#endif

    if (constrain_or_set(self, node, poly)) {
#if DEBUG_RESOLVE
        str node_str = v2_ast_node_to_string(self->transient, node);
        str poly_str = tl_polytype_to_string(self->transient, poly);
        fprintf(stderr, "[DEBUG process_annotation] ERROR: constrain_or_set failed for %s : %s\n",
                str_cstr(&node_str), str_cstr(&poly_str));
#endif
        return -1;
    }

    return 1;
}

// ============================================================================
// Special case handlers
// ============================================================================

static int is_std_function(ast_node *node) {
    return ast_node_is_std_application(node);
}

// ============================================================================
// Cast annotation helpers
// ============================================================================
//
// Cast annotations (x: CInt = expr, field: Ptr(Foo) = ptr_expr) suppress
// normal type-error behavior.  Integer casts skip constraint entirely to
// prevent narrow-type back-propagation; pointer casts constrain with error
// suppression for generic resolution.

int is_cast_annotation(ast_node *node) {
    if (!ast_node_is_symbol(node) || !node->symbol.annotation_type) return 0;
    tl_monotype *type = node->symbol.annotation_type->type;
    return tl_monotype_is_ptr(type) || tl_monotype_is_integer_convertible(type) ||
           tl_monotype_is_float_convertible(type);
}

// Handle cast annotation in let-in bindings.
// Integer casts: skip constraint + range check + weak-int resolve.
// Pointer casts: constrain with error suppression (skip if CArray).
static int cast_constrain_let_in(tl_infer *self, ast_node *node) {
    tl_polytype *annotation_type    = node->let_in.name->symbol.annotation_type;
    tl_polytype *value_type         = node->let_in.value->type;

    int          saved              = self->is_constrain_ignore_error;
    self->is_constrain_ignore_error = 1;

    if (tl_monotype_is_integer_convertible(annotation_type->type)) {
        // Integer cast: do not constrain (would back-propagate narrow type upstream).
        // Check literal range at compile time; look through unary minus for e.g. -129.
        ast_node *val    = node->let_in.value;
        int       negate = 0;
        if (val->tag == ast_unary_op && str_eq(ast_node_str(val->unary_op.op), S("-"))) {
            val    = val->unary_op.operand;
            negate = 1;
        }
        if (val->tag == ast_i64 || val->tag == ast_u64) {
            i64 lit = (val->tag == ast_i64) ? ast_node_i64(val)->val : (i64)ast_node_u64(val)->val;
            if (negate) lit = -lit;
            if (!tl_monotype_integer_value_fits(annotation_type->type, lit)) {
                log_type_error(self, annotation_type, value_type, node);
                type_error(self, node);
                self->is_constrain_ignore_error = saved;
                return 1;
            }
        }
        // Weak integer literals resolve to the annotation type rather than
        // defaulting to the canonical type (Int/UInt).
        if (value_type && tl_monotype_is_weak_int(value_type->type)) {
            constrain(self, annotation_type, value_type, node, TL_UNIFY_DIRECTED);
        }
    } else if (tl_monotype_is_float_convertible(annotation_type->type)) {
        // Float cast: do not constrain (would back-propagate narrow type upstream).
        // Weak float literals resolve to the annotation type.
        if (value_type && tl_monotype_is_weak_float(value_type->type)) {
            constrain(self, annotation_type, value_type, node, TL_UNIFY_DIRECTED);
        }
        // Weak int to float: no constraint needed, weak int defaults and cast handles it.
    } else {
        // Pointer cast: constrain with error suppression (the value may be a
        // generic that needs the annotation to resolve).
        // Skip CArray values — they decay to pointers without constraint.
        if (value_type) {
            tl_polytype_substitute(self->arena, value_type, self->subs);
            if (!tl_monotype_is_carray(value_type->type))
                constrain(self, annotation_type, value_type, node, TL_UNIFY_DIRECTED);
        }
    }

    self->is_constrain_ignore_error = saved;
    return 0;
}

// Handle cast annotation in struct field initialization.
// Two-phase: constrain annotation vs field (strict), then value vs field (permissive).
static int cast_constrain_struct_field(tl_infer *self, ast_node *arg, tl_monotype *field_type,
                                       ast_node const *node) {
    tl_polytype *annotation_type = arg->assignment.name->symbol.annotation_type;
    // Constrain annotation type against struct field to propagate concrete type info
    if (constrain_pm(self, annotation_type, field_type, node, TL_UNIFY_SYMMETRIC)) return 1;
    // Constrain value type against field permissively (cast)
    int saved                       = self->is_constrain_ignore_error;
    self->is_constrain_ignore_error = 1;
    constrain_pm(self, arg->type, field_type, node, TL_UNIFY_SYMMETRIC);
    self->is_constrain_ignore_error = saved;
    return 0;
}

// ============================================================================
// Per-node type inference
// ============================================================================
//
// Each infer_*() function handles bottom-up type inference for a specific AST
// node kind: literals, if/else, case/match, binary/unary ops, assignments,
// lambdas, named function applications, struct access, etc.  They generate type
// constraints via constrain() and attach polytypes to AST nodes.

static int infer_nil(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
    return constrain_pm(self, node->type, weak, node, TL_UNIFY_SYMMETRIC);
}

static int infer_void(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (ctx->node_pos == npos_operand) {
        tl_monotype *nil = tl_type_registry_nil(self->registry);
        ast_node_type_set(node, tl_polytype_absorb_mono(self->arena, nil));
    }
    return 0;
}

static int infer_body(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    if (node->body.expressions.size) {
        u32       sz   = node->body.expressions.size;
        ast_node *last = node->body.expressions.v[sz - 1];
        ensure_tv(self, &last->type);

        if (ast_node_is_lambda_function(last)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = last}));
            return 1;
        }

        return constrain(self, node->type, last->type, node, TL_UNIFY_SYMMETRIC);
    }
    return 0;
}

static int infer_tuple(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    ast_node_sized arr = ast_node_sized_from_ast_array(node);
    assert(arr.size > 0);

    tl_monotype_array tup_types = {.alloc = self->arena};
    array_reserve(tup_types, arr.size);
    forall(i, arr) {
        if (tl_polytype_is_scheme(arr.v[i]->type)) fatal("generic type");
        array_push(tup_types, arr.v[i]->type->type);
    }

    tl_monotype *tuple = tl_monotype_create_tuple(self->arena, (tl_monotype_sized)sized_all(tup_types));
    return constrain(self, node->type, tl_polytype_absorb_mono(self->arena, tuple), node,
                     TL_UNIFY_SYMMETRIC);
}

static int infer_while(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    return constrain_pm(self, node->type, nil, node, TL_UNIFY_SYMMETRIC);
}

static int infer_continue(tl_infer *self, ast_node *node) {
    ensure_tv(self, &node->type);
    return constrain_pm(self, node->type, tl_monotype_create_any(self->arena), node, TL_UNIFY_SYMMETRIC);
}

static int infer_return(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->return_.value, ctx, npos_operand)) return 1;

    if (node->return_.value && ast_node_is_lambda_function(node->return_.value)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_cannot_return_lambda, .node = node}));
        return 1;
    }

    ensure_tv(self, &node->type);
    if (!node->return_.is_break_statement && node->return_.value)
        if (constrain(self, node->type, node->return_.value->type, node, TL_UNIFY_SYMMETRIC)) return 1;

    if (ctx->result_type && node->return_.value) {
        tl_polytype wrap_result = tl_polytype_wrap(ctx->result_type);
        if (constrain(self, &wrap_result, node->return_.value->type, node, TL_UNIFY_DIRECTED)) return 1;
    }

    return 0;
}

static int infer_try(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // try expr: expr must be a two-variant tagged union.
    // Unwraps first variant (success), or early-returns second variant (error).

    tl_monotype *operand_type = node->try_.operand->type->type;
    tl_monotype_substitute(self->arena, operand_type, self->subs, null);

    // Must be a type constructor instance (the tagged union wrapper struct)
    if (!tl_monotype_is_inst(operand_type)) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_try_requires_two_variant_union, .node = node}));
        return 1;
    }

    // Find the union field in the wrapper type
    i32 u_index = tl_monotype_type_constructor_field_index(operand_type, S(AST_TAGGED_UNION_UNION_FIELD));
    if (u_index < 0) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_try_requires_two_variant_union, .node = node}));
        return 1;
    }

    tl_monotype *union_type = operand_type->cons_inst->args.v[u_index];

    // Must have exactly 2 variants
    if (!tl_monotype_is_inst(union_type) || union_type->cons_inst->def->field_names.size != 2) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_try_requires_two_variant_union, .node = node}));
        return 1;
    }

    // First variant is the success type, second is the error type
    tl_monotype *success_type = union_type->cons_inst->args.v[0];

    // Require success variant to have exactly one field so we can unwrap unambiguously
    if (!tl_monotype_is_inst(success_type) || success_type->cons_inst->def->field_names.size != 1) {
        array_push(self->errors,
                   ((tl_infer_error){.tag = tl_err_try_requires_single_field_variant, .node = node}));
        return 1;
    }
    tl_monotype *inner_type = success_type->cons_inst->args.v[0];

    // Constrain the try expression's type to the inner field type (full unwrap)
    ensure_tv(self, &node->type);
    if (constrain_pm(self, node->type, inner_type, node, TL_UNIFY_SYMMETRIC)) return 1;

    // Constrain the enclosing function's return type to be compatible with the wrapper type
    if (ctx->result_type)
        if (constrain_pm(self, node->try_.operand->type, ctx->result_type, node, TL_UNIFY_SYMMETRIC))
            return 1;

    return 0;
}

static int check_const_strip_in_call(tl_infer *, tl_monotype *, tl_polytype *, ast_node *);

static int infer_lambda_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    tl_monotype *inst =
      tl_polytype_instantiate(self->arena, node->lambda_application.lambda->type, self->subs);
    ast_node_type_set(node->lambda_application.lambda, tl_polytype_absorb_mono(self->arena, inst));

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    tl_polytype       *app  = make_arrow(self, ctx, iter.nodes, node, 0);
    if (!app) return 1;

    if (self->verbose) {
        str inst_str = tl_monotype_to_string(self->transient, inst);
        str app_str  = tl_polytype_to_string(self->transient, app);
        dbg(self, "application: anon lambda %.*s callsite arrow: %.*s", str_ilen(inst_str),
            str_buf(&inst_str), str_ilen(app_str), str_buf(&app_str));
    }

    if (check_const_strip_in_call(self, inst, app, node)) return 1;
    tl_polytype wrap = tl_polytype_wrap(inst);
    if (constrain(self, &wrap, app, node, TL_UNIFY_DIRECTED)) return 1;

    if (constrain(self, node->type, node->lambda_application.lambda->lambda_function.body->type, node,
                  TL_UNIFY_SYMMETRIC))
        return 1;

    return 0;
}

static int infer_lambda_function(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ensure_tv(self, &node->type);

    tl_polytype *arrow =
      make_arrow(self, ctx, ast_node_sized_from_ast_array(node), node->lambda_function.body, 1);
    if (!arrow) return 1;
    tl_polytype_generalize(arrow, self->env, self->subs);
    if (constrain(self, node->type, arrow, node, TL_UNIFY_SYMMETRIC)) return 1;

    return 0;
}

static int infer_if_then_else(tl_infer *self, ast_node *node) {
    tl_monotype *bool_type = tl_type_registry_bool(self->registry);
    if (constrain_pm(self, node->if_then_else.condition->type, bool_type, node, TL_UNIFY_SYMMETRIC))
        return 1;

    ensure_tv(self, &node->type);
    if (node->if_then_else.no) {
        if (constrain(self, node->if_then_else.yes->type, node->if_then_else.no->type, node,
                      TL_UNIFY_EXACT))
            return 1;
        if (constrain(self, node->type, node->if_then_else.yes->type, node, TL_UNIFY_SYMMETRIC)) return 1;
    } else {
        tl_monotype *nil      = tl_type_registry_nil(self->registry);
        tl_monotype *any_type = tl_monotype_create_any(self->arena);
        if (constrain_pm(self, node->type, nil, node, TL_UNIFY_SYMMETRIC)) return 1;

        if (constrain_pm(self, node->if_then_else.yes->type, any_type, node, TL_UNIFY_SYMMETRIC)) return 1;
    }

    return 0;
}

static int infer_assignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // Struct field assignment only
    if (!node->assignment.is_field_name) fatal("runtime error");

    ctx->is_field_name = node->assignment.is_field_name;
    if (resolve_node(self, node->assignment.name, ctx, npos_assign_lhs)) return 1;
    ctx->is_field_name = 0;
    if (resolve_node(self, node->assignment.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);

    if (constrain(self, node->type, node->assignment.value->type, node, TL_UNIFY_SYMMETRIC)) return 1;

    // Do not constrain name type because field names are not unique

    return 0;
}

// Walk two types through Ptr layers in parallel. Returns 1 if the arg type
// has Const at any nesting level where the param type does not.
static int types_strip_const(tl_monotype *param, tl_monotype *arg) {
    while (tl_monotype_is_ptr(param) && tl_monotype_is_ptr(arg)) {
        tl_monotype *pt = tl_monotype_ptr_target(param);
        tl_monotype *at = tl_monotype_ptr_target(arg);
        if (tl_monotype_is_const(at) && !tl_monotype_is_const(pt)) return 1;
        // Unwrap Const if present on both sides before continuing
        if (tl_monotype_is_const(pt)) pt = tl_monotype_const_target(pt);
        if (tl_monotype_is_const(at)) at = tl_monotype_const_target(at);
        param = pt;
        arg   = at;
    }
    return 0;
}

// Check if a function call strips const from pointer arguments.
// Compares function parameter types against callsite argument types before unification.
static int check_const_strip_in_call(tl_infer *self, tl_monotype *func_type, tl_polytype *callsite,
                                     ast_node *node) {
    if (!tl_monotype_is_arrow(func_type)) return 0;
    tl_monotype *call_mono = callsite->type;
    if (!tl_monotype_is_arrow(call_mono)) return 0;

    tl_monotype_sized func_params = tl_monotype_arrow_get_args(func_type);
    tl_monotype_sized call_args   = tl_monotype_arrow_get_args(call_mono);

    u32               n           = func_params.size < call_args.size ? func_params.size : call_args.size;
    for (u32 i = 0; i < n; ++i) {
        if (types_strip_const(func_params.v[i], call_args.v[i])) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_const_violation, .node = node}));
            return 1;
        }
    }
    return 0;
}

// Check if the LHS of a reassignment involves dereferencing a Ptr(Const(T)).
// Returns 1 if a const violation is detected.
static int check_const_violation(tl_infer *self, ast_node *lhs) {
    if (!lhs) return 0;
    (void)self;

    // ptr.* = value: unary dereference of a const pointer
    if (lhs->tag == ast_unary_op && str_eq(ast_node_str(lhs->unary_op.op), S("*"))) {
        ast_node *operand = lhs->unary_op.operand;
        if (operand->type && operand->type->type) {
            tl_monotype *t = operand->type->type;
            if (tl_monotype_is_ptr_to_const(t)) return 1;
        }
    }

    // ptr->field = value, ptr.field = value, or ptr[i] = value
    if (lhs->tag == ast_binary_op) {
        str         op   = ast_node_str(lhs->binary_op.op);
        char const *op_s = str_cstr(&op);
        if (is_struct_access_operator(op_s) || is_index_operator(op_s)) {
            ast_node *left = lhs->binary_op.left;
            if (left->type && left->type->type) {
                tl_monotype *t = left->type->type;
                if (tl_monotype_is_ptr_to_const(t)) return 1;
            }
        }
    }

    return 0;
}

static int infer_reassignment(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    // Reassign to let-in bound symbol
    if (node->assignment.is_field_name) fatal("runtime error");

    if (resolve_node(self, node->assignment.name, ctx, npos_reassign_lhs)) return 1;
    if (resolve_node(self, node->assignment.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);

    // Check for const violations on the LHS
    if (check_const_violation(self, node->assignment.name)) {
        array_push(self->errors, ((tl_infer_error){.tag = tl_err_const_violation, .node = node}));
        return 1;
    }

    // reassignment nodes have void type
    tl_monotype *nil = tl_type_registry_nil(self->registry);
    if (constrain_pm(self, node->type, nil, node, TL_UNIFY_SYMMETRIC)) return 1;

    // For compound assignment (+=, -=, etc.) the value is an arithmetic operand,
    // so require exact type match — same rule as binary operators.
    // For plain reassignment (=), widening is allowed via directed unification.
    int mode = (node->tag == ast_reassignment_op) ? TL_UNIFY_EXACT : TL_UNIFY_DIRECTED;
    if (constrain(self, node->assignment.name->type, node->assignment.value->type, node, mode)) return 1;

    return 0;
}

static int infer_binary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    ast_node   *left = node->binary_op.left, *right = node->binary_op.right;
    char const *op = str_cstr(&node->binary_op.op->symbol.name);

    if (resolve_node(self, left, ctx, npos_operand)) return 1;
    if (resolve_node(self, right, ctx, is_struct_access_operator(op) ? npos_field_name : npos_operand))
        return 1;

    ensure_tv(self, &node->type);

    if (is_arithmetic_operator(op)) {
        if (constrain(self, node->type, left->type, node, TL_UNIFY_SYMMETRIC)) return 1;
        if (constrain(self, left->type, right->type, node, TL_UNIFY_EXACT)) return 1;
    } else if (is_bitwise_operator(op)) {
        // Bitwise operators: operands must match each other (same integer family).
        if (constrain(self, node->type, left->type, node, TL_UNIFY_SYMMETRIC)) return 1;
        if (constrain(self, left->type, right->type, node, TL_UNIFY_EXACT)) return 1;
    } else if (is_logical_operator(op) || is_relational_operator(op)) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node, TL_UNIFY_SYMMETRIC)) return 1;
        // Relational/logical: operands stay SYMMETRIC for now (result is Bool, not integer).
        // Phase 4+ may tighten this once stdlib CSize/UInt mixing is resolved.
        if (constrain(self, left->type, right->type, node, TL_UNIFY_SYMMETRIC)) return 1;
    } else if (is_index_operator(op)) {

        // needed
        tl_monotype_substitute(self->arena, left->type->type, self->subs, null);
        tl_monotype_substitute(self->arena, right->type->type, self->subs, null);

        if (tl_monotype_has_ptr(left->type->type)) {
            tl_monotype *target = tl_monotype_ptr_target(left->type->type);
            if (constrain_pm(self, node->type, target, node, TL_UNIFY_SYMMETRIC)) return 1;
        } else if (tl_monotype_is_carray(left->type->type)) {
            tl_monotype *target = tl_monotype_carray_element(left->type->type);
            if (constrain_pm(self, node->type, target, node, TL_UNIFY_SYMMETRIC)) return 1;
        }

    } else if (is_struct_access_operator(op)) {
        if (infer_struct_access(self, ctx, node)) return 1;
    } else {
        fatal("unknown operator type");
    }

    return 0;
}

static int infer_unary_op(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->unary_op.operand, ctx, npos_operand)) return 1;
    ast_node *operand = node->unary_op.operand;
    ensure_tv(self, &node->type);

    str op = ast_node_str(node->unary_op.op);
    if (str_eq(op, S("*"))) {
        tl_polytype_substitute(self->arena, operand->type, self->subs); // needed
        if (tl_monotype_has_ptr(operand->type->type)) {
            assert(!tl_polytype_is_scheme(operand->type));
            tl_monotype *target = tl_monotype_ptr_target(operand->type->type);
            if (constrain_pm(self, node->type, target, node, TL_UNIFY_SYMMETRIC)) return 1;
        } else if (tl_polytype_is_concrete(operand->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    } else if (str_eq(op, S("&"))) {
        if (!tl_polytype_is_scheme(operand->type)) {
            tl_monotype *ptr = tl_type_registry_ptr(self->registry, operand->type->type);
            if (constrain_pm(self, node->type, ptr, node, TL_UNIFY_SYMMETRIC)) return 1;
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, node->type, ptr, node, TL_UNIFY_SYMMETRIC)) return 1;
        }
    } else if (str_eq(op, S("!"))) {
        tl_monotype *bool_type = tl_type_registry_bool(self->registry);
        if (constrain_pm(self, node->type, bool_type, node, TL_UNIFY_SYMMETRIC)) return 1;
    } else if (str_eq(op, S("~")) || str_eq(op, S("-")) || str_eq(op, S("+"))) {
        if (constrain(self, node->type, operand->type, node, TL_UNIFY_SYMMETRIC)) return 1;
    } else {
        fatal("unknown unary operator");
    }

    return 0;
}

// Find a variant type by name in a tagged union wrapper type.
// Looks up the 'u' (union) field in the wrapper, then searches by variant name.
// Returns the variant's monotype, or null if the wrapper has no 'u' field or variant not found.
// If out_index is non-null, stores the variant's index within the union.
tl_monotype *tagged_union_find_variant(tl_monotype *wrapper_type, str variant_name, int *out_index) {
    i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S(AST_TAGGED_UNION_UNION_FIELD));
    if (u_index < 0) return null;
    tl_monotype *union_type  = wrapper_type->cons_inst->args.v[u_index];
    str_sized    field_names = union_type->cons_inst->def->field_names;
    forall(j, field_names) {
        if (str_eq(field_names.v[j], variant_name)) {
            if (out_index) *out_index = (int)j;
            return union_type->cons_inst->args.v[j];
        }
    }
    return null;
}

// Wrap a variant type in Ptr if the binding is mutable (&), otherwise return as-is.
static tl_polytype *tagged_union_variant_poly(tl_infer *self, tl_monotype *variant_type,
                                              int is_union_flag) {
    if (is_union_flag == AST_TAGGED_UNION_MUTABLE)
        return tl_polytype_absorb_mono(self->arena, tl_type_registry_ptr(self->registry, variant_type));
    return tl_polytype_absorb_mono(self->arena, variant_type);
}

static int infer_tagged_union_case(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    tl_polytype *expr_type = node->case_.expression->type;

    // Get wrapper type and extract valid variants
    tl_monotype *wrapper_type = expr_type->type;
    tl_monotype_substitute(self->arena, wrapper_type, self->subs, null); // needed

    // If there is an explicit type annotation (e.g., "case x: Option(T)"), always parse and
    // use it as the wrapper type. This is essential for generic functions with type-predicate
    // branching (e.g., `if x :: Option { case x: Option(T) { ... } } else if x :: Result ...`)
    // where different branches constrain the same variable to different tagged union types.
    // Without this, the first branch's constraint would permanently unify the variable's type,
    // making subsequent branches fail. The constraint is non-fatal here because after
    // specialization, only the matching branch will be valid.
    if (node->case_.union_annotation) {
        annotation_parse_result result = parse_type_annotation(self, ctx, node->case_.union_annotation);
        if (result.parsed && tl_monotype_is_inst(result.parsed)) {
            wrapper_type = result.parsed;
            // Constrain expression type to match annotation (non-fatal: in generic functions,
            // a prior branch may have already constrained to a different type)
            int save                        = self->is_constrain_ignore_error;
            self->is_constrain_ignore_error = 1;
            constrain_pm(self, expr_type, wrapper_type, node->case_.expression, TL_UNIFY_SYMMETRIC);
            self->is_constrain_ignore_error = save;
            tl_monotype_substitute(self->arena, wrapper_type, self->subs, null);
        }
    }

    if (!tl_monotype_is_inst(wrapper_type)) {
        expected_tagged_union(self, node->case_.expression);
        return 1;
    }

    // Find the union field in the wrapper type
    i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S(AST_TAGGED_UNION_UNION_FIELD));
    if (u_index < 0) {
        expected_tagged_union(self, node->case_.expression);
        return 1;
    }

    tl_monotype *union_type     = wrapper_type->cons_inst->args.v[u_index];
    str_sized    valid_variants = union_type->cons_inst->def->field_names;

    // Track which variants are covered (for exhaustiveness checking)
    int *variant_covered = alloc_malloc(self->transient, valid_variants.size * sizeof(int));
    memset(variant_covered, 0, valid_variants.size * sizeof(int));
    int has_else_arm = 0;

    forall(i, node->case_.conditions) {
        // Detect `else` condition on final arm
        if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) {
            has_else_arm = 1;
            break;
        }

        ast_node *cond = node->case_.conditions.v[i];
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) {
            // "tagged union case condition must be 'binding: VariantType'"
            tagged_union_case_syntax_error(self, cond);
        }

        // Find the variant by name in the wrapper type
        str          variant_name  = ast_node_name_original(cond->symbol.annotation);
        int          variant_found = -1;
        tl_monotype *variant_type  = tagged_union_find_variant(wrapper_type, variant_name, &variant_found);

        if (!variant_type) {
            array_push(self->errors,
                       ((tl_infer_error){.tag = tl_err_tagged_union_unknown_variant, .node = cond}));
            return 1;
        }

        // Mark this variant as covered
        variant_covered[variant_found] = 1;

        // Set the binding's type (not as a literal - this is a value, not a type expression).
        // Note that we set both the condition node and the annotation_type.
        // If the case variable is mutable (var.&), we have a pointer type.
        tl_polytype *variant_poly = tagged_union_variant_poly(self, variant_type, node->case_.is_union);

        ast_node_type_set(cond, variant_poly);
        cond->symbol.annotation_type = variant_poly;
    }

    // Exhaustiveness check: if no else arm, verify all variants are covered
    if (!has_else_arm) {
        forall(j, valid_variants) {
            if (!variant_covered[j]) {
                array_push(self->errors,
                           ((tl_infer_error){.tag = tl_err_tagged_union_missing_case, .node = node}));
                return 1;
            }
        }
    }

    return 0;
}

static int infer_case(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->case_.expression, ctx, npos_operand)) return 1;
    if (resolve_node(self, node->case_.binary_predicate, ctx, npos_operand)) return 1;

    ensure_tv(self, &node->type);
    tl_monotype *nil       = tl_type_registry_nil(self->registry);
    tl_monotype *any_type  = tl_monotype_create_any(self->arena);
    tl_polytype *expr_type = node->case_.expression->type;

    if (node->case_.conditions.size != node->case_.arms.size) fatal("logic error");

    if (node->case_.is_union) {
        if (infer_tagged_union_case(self, ctx, node)) return 1;
    } else {
        // Standard case expression: conditions are expressions compared for equality
        forall(i, node->case_.conditions) {
            // Detect `else` condition on final arm
            if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) break;

            if (resolve_node(self, node->case_.conditions.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.conditions.v[i]->type);
            if (constrain(self, expr_type, node->case_.conditions.v[i]->type, node, TL_UNIFY_SYMMETRIC))
                return 1;
        }
    }

    switch (node->case_.arms.size) {
    case 0:
        if (constrain_pm(self, node->type, nil, node, TL_UNIFY_SYMMETRIC)) return 1;
        break;
    case 1:
        if (constrain_pm(self, node->type, nil, node, TL_UNIFY_SYMMETRIC)) return 1;
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        if (constrain_pm(self, node->case_.arms.v[0]->type, any_type, node, TL_UNIFY_SYMMETRIC)) return 1;
        break;

    default: {
        if (resolve_node(self, node->case_.arms.v[0], ctx, npos_operand)) return 1;
        tl_polytype *arm_type = node->case_.arms.v[0]->type;
        if (constrain(self, node->type, arm_type, node->case_.arms.v[0], TL_UNIFY_SYMMETRIC)) return 1;

        forall(i, node->case_.arms) {
            if (resolve_node(self, node->case_.arms.v[i], ctx, npos_operand)) return 1;
            ensure_tv(self, &node->case_.arms.v[i]->type);
            if (constrain(self, node->case_.arms.v[i]->type, arm_type, node, TL_UNIFY_EXACT)) return 1;
        }
    } break;
    }

    if (node->case_.binary_predicate && node->case_.conditions.size) {
        tl_polytype *pred_arrow =
          make_binary_predicate_arrow(self, ctx, node->case_.expression, node->case_.conditions.v[0]);
        if (constrain(self, node->case_.binary_predicate->type, pred_arrow, node, TL_UNIFY_SYMMETRIC))
            return 1;
    }

    return 0;
}

static int infer_let_in(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node->let_in.name, ctx, npos_formal_parameter)) return 1;
    if (resolve_node(self, node->let_in.value, ctx, npos_value_rhs)) return 1;

    ensure_tv(self, &node->type);
    if (node->let_in.body) ensure_tv(self, &node->let_in.body->type);

    if (ast_node_is_lambda_function(node->let_in.value)) {
        str name = node->let_in.name->symbol.name;

        if (add_generic(self, node)) return 1;

        node->let_in.name->type = null;

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node, TL_UNIFY_SYMMETRIC)) return 1;

        {
            ast_node *let_in_lambda    = ast_node_clone(self->arena, node);
            let_in_lambda->let_in.body = null;
            toplevel_add(self, name, let_in_lambda);
        }

    } else {
        if (is_std_function(node->let_in.value)) {
            node->let_in.value->type = tl_polytype_nil(self->arena, self->registry);
        }

        tl_polytype *name_type            = node->let_in.name->type;
        tl_polytype *name_annotation_type = node->let_in.name->symbol.annotation_type;
        tl_polytype *value_type           = node->let_in.value->type;

        if (name_annotation_type) {
            name_type = name_annotation_type;

            str name  = ast_node_str(node->let_in.name);
            str tmp   = tl_polytype_to_string(self->transient, name_annotation_type);

            dbg(self, "let_in cast '%s': using annotation type '%s'", str_cstr(&name), str_cstr(&tmp));
        }

        if (is_cast_annotation(node->let_in.name)) {
            if (cast_constrain_let_in(self, node)) return 1;
        } else {
            if (constrain(self, name_type, value_type, node, TL_UNIFY_DIRECTED)) return 1;
        }

        env_insert_constrain(self, node->let_in.name->symbol.name, name_type, node->let_in.name);

        if (node->let_in.body)
            if (constrain(self, node->type, node->let_in.body->type, node, TL_UNIFY_SYMMETRIC)) return 1;
    }
    return 0;
}

// Infer type constructor application: Foo(a=1, b=2), Foo(Int), etc.
static int infer_type_constructor_nfa(tl_infer *self, traverse_ctx *ctx, ast_node *node, str name) {
    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *arg;
    while ((arg = ast_arguments_next(&iter))) {
        if (resolve_node(self, arg, ctx, npos_function_argument)) return 1;
    }

    tl_monotype *inst        = null;
    u32          n_type_args = node->named_application.n_type_arguments;

    if (n_type_args > 0) {
        // Use explicit type arguments for instantiation
        tl_monotype_sized args = {
          .v    = alloc_malloc(self->transient, n_type_args * sizeof(tl_monotype *)),
          .size = n_type_args,
        };

        for (u32 i = 0; i < n_type_args; i++) {
            ast_node *type_arg_node = node->named_application.type_arguments[i];
            if (type_arg_node && type_arg_node->type) {
                args.v[i] = type_arg_node->type->type;
#if DEBUG_EXPLICIT_TYPE_ARGS
                str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
                fprintf(stderr,
                        "[DEBUG EXPLICIT TYPE ARGS] type constructor: using explicit type for arg %u: %s\n",
                        i, str_cstr(&arg_str));
#endif
            } else {
                args.v[i] = null; // will create fresh type variable
            }
        }

        inst = tl_type_registry_instantiate_with(self->registry, name, args);
    } else {
        inst = tl_type_registry_instantiate(self->registry, name);
    }

    if (!inst) {
        wrong_number_of_arguments(self, node);
        return 1;
    }

#if DEBUG_EXPLICIT_TYPE_ARGS
    {
        str inst_str = tl_monotype_to_string(self->transient, inst);
        fprintf(stderr,
                "[DEBUG EXPLICIT TYPE ARGS] infer_named_function_application (type constructor):\n");
        fprintf(stderr, "  name: %s\n", str_cstr(&name));
        fprintf(stderr, "  instantiated type: %s\n", str_cstr(&inst_str));
        fprintf(stderr, "  inst->cons_inst->args.size: %u\n", (u32)inst->cons_inst->args.size);
        for (u32 j = 0; j < inst->cons_inst->args.size; j++) {
            str arg_str = tl_monotype_to_string(self->transient, inst->cons_inst->args.v[j]);
            fprintf(stderr, "    field[%u] type: %s\n", j, str_cstr(&arg_str));
        }
    }
#endif

    {
        tl_polytype *app = make_arrow(self, ctx, iter.nodes, null, 0);
        if (!app) return 1;

        if (self->verbose) {
            str inst_str = tl_monotype_to_string(self->transient, inst);
            str app_str  = tl_polytype_to_string(self->transient, app);
            dbg(self, "type constructor: callsite '%s' (%s) arrow: %s", str_cstr(&name),
                str_cstr(&inst_str), str_cstr(&app_str));
        }
    }

    if (!is_union_struct(self, name)) {
        iter = ast_node_arguments_iter(node);
        if (iter.nodes.size != inst->cons_inst->args.size) {
            wrong_number_of_arguments(self, node);
            return 1;
        }
    }

    u32 i = 0;
    iter  = ast_node_arguments_iter(node);
    while ((arg = ast_arguments_next(&iter))) {
        if (i >= inst->cons_inst->args.size) fatal("runtime error");

        if (ast_node_is_assignment(arg)) {
            // This is a type value constructor
            i32 found =
              tl_monotype_type_constructor_field_index(inst, ast_node_name_original(arg->assignment.name));

            if (-1 == found) {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_field_not_found, .node = arg}));
                return 1;
            }
            assert(found < (i32)inst->cons_inst->args.size);

            if (is_cast_annotation(arg->assignment.name)) {
                if (cast_constrain_struct_field(self, arg, inst->cons_inst->args.v[found], node)) return 1;
            } else {
#if DEBUG_EXPLICIT_TYPE_ARGS
                {
                    str field_name = ast_node_name_original(arg->assignment.name);
                    str field_type_str =
                      tl_monotype_to_string(self->transient, inst->cons_inst->args.v[found]);
                    fprintf(stderr, "[DEBUG EXPLICIT TYPE ARGS] constraining field '%s':\n",
                            str_cstr(&field_name));
                    if (arg->type) {
                        str arg_type_str = tl_polytype_to_string(self->transient, arg->type);
                        fprintf(stderr, "  arg->type (value): %s\n", str_cstr(&arg_type_str));
                    } else {
                        fprintf(stderr, "  arg->type (value): (null)\n");
                    }
                    fprintf(stderr, "  field type from inst: %s\n", str_cstr(&field_type_str));
                }
#endif
                if (constrain_pm(self, arg->type, inst->cons_inst->args.v[found], node, TL_UNIFY_SYMMETRIC))
                    return 1;
            }
        } else {
            // In this branch, node is a type literal.
        }
        ++i;
    }

    return constrain_pm(self, node->type, inst, node, TL_UNIFY_SYMMETRIC);
}

static int infer_named_function_application(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (resolve_node(self, node, ctx, ctx->node_pos)) return 1;

    str          name     = ast_node_str(node->named_application.name);
    str          original = ast_node_name_original(node->named_application.name);
    tl_polytype *type     = tl_type_env_lookup(self->env, name);
    if (!type) {
        type = tl_type_registry_get(self->registry, name);
        if (!type) return 0;
    }

    if (is_type_literal(self, ctx, node)) return 0;

    if (tl_polytype_is_type_constructor(type)) {
        return infer_type_constructor_nfa(self, ctx, node, name);
    } else {
        if (tl_polytype_is_concrete(type)) {
            if (!str_is_empty(original)) {
                tl_polytype *found = tl_type_env_lookup(self->env, original);
                if (found) type = found;
            }
        }

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        tl_monotype       *inst = null;

        // Check for explicit type arguments
        u32       n_type_args = node->named_application.n_type_arguments;
        ast_node *let         = toplevel_get(self, name);
        u32       n_quants    = type->quantifiers.size;

        if (n_type_args > 0 && let && ast_node_is_let(let) && n_quants > 0) {
            // Build args array from explicit type arguments
            tl_monotype_sized args = {
              .v    = alloc_malloc(self->transient, n_quants * sizeof(tl_monotype *)),
              .size = n_quants,
            };

            for (u32 i = 0; i < n_quants; i++) {
                args.v[i] = null; // default: create fresh type variable

                if (i < let->let.n_type_parameters) {
                    str param_name = let->let.type_parameters[i]->symbol.name;

#if DEBUG_INVARIANTS
                    // Invariant: Type argument lookup must use alpha-converted names
                    if (!is_alpha_converted_name(param_name)) {
                        char detail[256];
                        snprintf(detail, sizeof detail,
                                 "Looking up '%.*s' which is not alpha-converted in type_arguments",
                                 str_ilen(param_name), str_buf(&param_name));
                        report_invariant_failure(self, "infer_named_function_application",
                                                 "Type argument lookup must use alpha-converted names",
                                                 detail, node);
                    }
#endif

                    tl_monotype *explicit_type = str_map_get_ptr(ctx->type_arguments, param_name);
                    if (explicit_type) {
                        args.v[i] = explicit_type;
#if DEBUG_EXPLICIT_TYPE_ARGS
                        str arg_str = tl_monotype_to_string(self->transient, args.v[i]);
                        fprintf(stderr,
                                "[DEBUG EXPLICIT TYPE ARGS] using explicit type for quantifier %u: %s\n", i,
                                str_cstr(&arg_str));
#endif
                    }
                }
            }

            inst = tl_polytype_instantiate_with(self->arena, type, args, self->subs);
        } else {
            inst = tl_polytype_instantiate(self->arena, type, self->subs);
        }

        // Function reference with explicit type args: name[TypeArgs]/N
        // Set node type to the function type itself, not a call result.
        if (node->named_application.is_function_reference) {
            tl_polytype wrap = tl_polytype_wrap(inst);
            if (constrain(self, &wrap, node->type, node, TL_UNIFY_DIRECTED)) return 1;
            return 0;
        }

        str          inst_str = tl_monotype_to_string(self->transient, inst);
        tl_polytype *app      = make_arrow(self, ctx, iter.nodes, node, 0);
        if (!app) return 1;

#if DEBUG_EXPLICIT_TYPE_ARGS
        {
            str type_str = tl_polytype_to_string(self->transient, type);
            str app_str  = tl_polytype_to_string(self->transient, app);
            fprintf(stderr,
                    "[DEBUG EXPLICIT TYPE ARGS] infer_named_function_application (function call):\n");
            fprintf(stderr, "  name: %s\n", str_cstr(&name));
            fprintf(stderr, "  callee type from env: %s\n", str_cstr(&type_str));
            fprintf(stderr, "  instantiated: %s\n", str_cstr(&inst_str));
            fprintf(stderr, "  callsite arrow: %s\n", str_cstr(&app_str));
            fprintf(stderr, "  n_type_arguments: %u\n", node->named_application.n_type_arguments);
        }
#endif

        if (self->verbose) {
            str app_str = tl_polytype_to_string(self->transient, app);
            dbg(self, "application: callsite '%s' (%s) arrow: %s", str_cstr(&name), str_cstr(&inst_str),
                str_cstr(&app_str));
        }
        if (check_const_strip_in_call(self, inst, app, node)) return 1;
        tl_polytype wrap = tl_polytype_wrap(inst);
        if (constrain(self, &wrap, app, node, TL_UNIFY_DIRECTED)) return 1;
    }

    return 0;
}

// ============================================================================
// AST traversal implementation
// ============================================================================

int traverse_ast_node_params(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        assert(ast_node_is_symbol(param));

        ensure_tv(self, &param->type);

        ctx->node_pos = npos_formal_parameter;
        if (cb(self, ctx, param)) return 1;
    }
    return 0;
}

int traverse_ast(tl_infer *, traverse_ctx *, ast_node *, traverse_cb);

// Pre-set variant binding types on tagged union case conditions BEFORE traversing arm bodies.
// This is essential for nested when: the inner when needs the outer binding's type to be resolved
// so that field access (e.g., s.v) works and the inner scrutinee gets a concrete tagged union type.
// Without this, the bottom-up callback order means the inner when's infer_tagged_union_case runs
// before the outer when's, leaving binding types as unresolved type variables.
static void prepare_tagged_union_bindings(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    tl_polytype *expr_type = node->case_.expression->type;
    if (!expr_type) return;

    tl_monotype *wrapper_type = expr_type->type;
    tl_monotype_substitute(self->arena, wrapper_type, self->subs, null);

    // Handle explicit type annotation (when x: Type { ... })
    if (node->case_.union_annotation) {
        annotation_parse_result result = parse_type_annotation(self, ctx, node->case_.union_annotation);
        if (result.parsed && tl_monotype_is_inst(result.parsed)) {
            wrapper_type                    = result.parsed;
            int save                        = self->is_constrain_ignore_error;
            self->is_constrain_ignore_error = 1;
            constrain_pm(self, expr_type, wrapper_type, node->case_.expression, TL_UNIFY_SYMMETRIC);
            self->is_constrain_ignore_error = save;
            tl_monotype_substitute(self->arena, wrapper_type, self->subs, null);
        }
    }

    if (!tl_monotype_is_inst(wrapper_type))
        return; // type not yet resolved; defer to infer_tagged_union_case

    i32 u_index = tl_monotype_type_constructor_field_index(wrapper_type, S(AST_TAGGED_UNION_UNION_FIELD));
    if (u_index < 0) return;

    forall(i, node->case_.conditions) {
        if (i + 1 == node->case_.conditions.size && ast_node_is_nil(node->case_.conditions.v[i])) break;

        ast_node *cond = node->case_.conditions.v[i];
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) continue;

        // Don't overwrite an already-specialized annotation type (set by specialize_case).
        // Later traversals (Phase 7) re-enter this function but must not clobber specialization.
        if (cond->symbol.annotation_type &&
            tl_monotype_is_inst_specialized(cond->symbol.annotation_type->type))
            continue;

        str          variant_name = ast_node_name_original(cond->symbol.annotation);
        tl_monotype *variant_type = tagged_union_find_variant(wrapper_type, variant_name, null);
        if (!variant_type) continue; // will be caught later by infer_tagged_union_case

        tl_polytype *variant_poly = tagged_union_variant_poly(self, variant_type, node->case_.is_union);

        ast_node_type_set(cond, variant_poly);
        cond->symbol.annotation_type = variant_poly;
        env_insert_constrain(self, cond->symbol.name, variant_poly, cond);
    }
}

int traverse_ast_case(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    ctx->node_pos = npos_operand;
    if (traverse_ast(self, ctx, node->case_.expression, cb)) return 1;

    ctx->node_pos = npos_operand;
    if (traverse_ast(self, ctx, node->case_.binary_predicate, cb)) return 1;

    if (node->case_.is_union) {
        // Pre-set variant binding types so that nested when expressions can resolve
        // field accesses on outer bindings during their own bottom-up traversal.
        prepare_tagged_union_bindings(self, ctx, node);

        // For union cases, conditions are handled by infer_case() directly.
        // We only need to add condition symbols to lexical scope before traversing arms.
        forall(i, node->case_.conditions) {
            hashmap  *save = map_copy(ctx->lexical_names);
            ast_node *cond = node->case_.conditions.v[i];

            // Skip nil condition (else arm)
            if (ast_node_is_nil(cond)) {
                if (i < node->case_.arms.size) {
                    ctx->node_pos = npos_operand;
                    if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
                }
                ctx->lexical_names = save;
                continue;
            }

            // Add condition symbol to lexical scope (don't traverse it - infer_case handles it)
            if (ast_node_is_symbol(cond)) {
                str_hset_insert(&ctx->lexical_names, cond->symbol.name);
            }

            // Process only the corresponding arm (not the condition)
            if (i < node->case_.arms.size) {
                ctx->node_pos = npos_operand;
                if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
            }

            ctx->lexical_names = save;
        }
    } else {
        forall(i, node->case_.conditions) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->case_.conditions.v[i], cb)) return 1;
        }
        forall(i, node->case_.arms) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->case_.arms.v[i], cb)) return 1;
        }
    }
    return cb(self, ctx, node);
}

int traverse_ast(tl_infer *self, traverse_ctx *ctx, ast_node *node, traverse_cb cb) {
    if (null == node) return 0;
    if (self->report_stats) self->counters.traverse_nodes_visited++;

    switch (node->tag) {
    case ast_attribute_set:
        // not traversed
        break;

    case ast_let: {
        // Save outer context: when specializing nested functions (via post_specialize → specialize_arrow),
        // the inner function's let case would otherwise clobber the outer type_arguments and lexical_names.
        hashmap *save_type_arguments = map_copy(ctx->type_arguments);
        hashmap *save_lexical_names  = map_copy(ctx->lexical_names);

        // Note: this node is being processed as a toplevel function definition. It must clear all lexical
        // contexts.
        map_reset(ctx->type_arguments);
        map_reset(ctx->lexical_names);

        traverse_ctx_load_type_arguments(self, ctx, node);

        ctx->node_pos = npos_toplevel;
        // Note: traversing the name as a symbol currently causes invalid constraints to be applied when
        // specializing generic functions. The name's node->type should not in any case be relied upon: the
        // canonical arrow type of a function name is in the environment, not the ast.

        ctx->node_pos = npos_formal_parameter;
        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let.body, cb)) return 1;

        // Note: let nodes are intentionally not processed with the callback.

        // Restore outer context
        map_destroy(&ctx->type_arguments);
        map_destroy(&ctx->lexical_names);
        ctx->type_arguments = save_type_arguments;
        ctx->lexical_names  = save_lexical_names;

    } break;

    case ast_let_in: {

        hashmap *save = map_copy(ctx->lexical_names);
        assert(ast_node_is_symbol(node->let_in.name));

        // process name first, for lexical scope
        ctx->node_pos = npos_formal_parameter;
        if (cb(self, ctx, node->let_in.name)) return 1;

        // process node parent before children, because there may be side effects required before traversing
        // body.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        // traverse value first, then traverse name and body
        ctx->node_pos = npos_value_rhs;
        if (traverse_ast(self, ctx, node->let_in.value, cb)) return 1;

        ctx->node_pos = npos_formal_parameter;
        if (traverse_ast(self, ctx, node->let_in.name, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->let_in.body, cb)) return 1;

        // process node again: for specialised types, typing the name depends on typing the value.
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_named_function_application: {
        if (traverse_ctx_assign_type_arguments(self, ctx, node)) return 1;

        // traverse arguments

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_lambda_function: {

        hashmap *save = map_copy(ctx->lexical_names);

        if (traverse_ast_node_params(self, ctx, node, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_function.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

        map_destroy(&ctx->lexical_names);
        ctx->lexical_names = save;

    } break;

    case ast_lambda_function_application: {

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) {
            ctx->node_pos = npos_function_argument;
            if (traverse_ast(self, ctx, arg, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->lambda_application.lambda, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;

    } break;

    case ast_if_then_else: {

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.condition, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.yes, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->if_then_else.no, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, arr.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
    } break;

    case ast_body:
        forall(i, node->body.expressions) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->body.expressions.v[i], cb)) return 1;
        }
        forall(i, node->body.defers) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->body.defers.v[i], cb)) return 1;
        }
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_case:
        if (traverse_ast_case(self, ctx, node, cb)) return 1;
        break;

    case ast_binary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->binary_op.left, cb)) return 1;

        // when traversing to the right of . and ->, we could encounter field names that should not be
        // considered free variables, so signal that in the traverse_ctx. Note that other binary ops like
        // arithmetic should not trigger the field_name case.
        {
            char const *op               = str_cstr(&node->binary_op.op->symbol.name);
            int         is_symbol        = ast_node_is_symbol(node->binary_op.right);
            int         is_struct_access = is_struct_access_operator(op);
            int         is_field_name    = is_symbol && is_struct_access;
            int         save             = 0;
            if (is_field_name) {
                save               = ctx->is_field_name;
                ctx->is_field_name = 1;
            }

            if (is_struct_access_operator(op)) ctx->node_pos = npos_field_name;
            else ctx->node_pos = npos_operand;

            if (traverse_ast(self, ctx, node->binary_op.right, cb)) return 1;
            if (is_field_name) ctx->is_field_name = save;
        }

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_unary_op:
        // don't traverse op, it's just an operator
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->unary_op.operand, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_assignment:
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = node->assignment.is_field_name;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_value_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_reassignment:
    case ast_reassignment_op:
        // don't traverse op, it's just an operator
        ctx->node_pos      = npos_assign_lhs;
        ctx->is_field_name = node->assignment.is_field_name;
        if (traverse_ast(self, ctx, node->assignment.name, cb)) return 1;

        ctx->node_pos      = npos_value_rhs;
        ctx->is_field_name = 0;
        if (traverse_ast(self, ctx, node->assignment.value, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_return:
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->return_.value, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_try:
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->try_.operand, cb)) return 1;
        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_while:
        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.condition, cb)) return 1;

        if (node->while_.update) {
            ctx->node_pos = npos_operand;
            if (traverse_ast(self, ctx, node->while_.update, cb)) return 1;
        }

        ctx->node_pos = npos_operand;
        if (traverse_ast(self, ctx, node->while_.body, cb)) return 1;

        ctx->node_pos = npos_operand;
        if (cb(self, ctx, node)) return 1;
        break;

    case ast_hash_command:
    case ast_nil:
    case ast_void:
    case ast_continue:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_i64_z:
    case ast_string:
    case ast_char:
    case ast_symbol:
    case ast_u64:
    case ast_u64_zu:
    case ast_trait_definition:
    case ast_type_alias:
    case ast_type_predicate:
    case ast_user_type_definition:

        // operate on the leaf node
        if (cb(self, ctx, node)) return 1;

        break;
    }

    return 0;
}

// ============================================================================
// Symbol resolution and struct access
// ============================================================================

int constrain_or_set(tl_infer *self, ast_node *node, tl_polytype *type) {
#if DEBUG_RESOLVE
    str name     = ast_node_is_symbol(node) ? ast_node_str(node) : str_empty();
    str poly_str = tl_polytype_to_string(self->transient, type);
#endif

    if (node->type) {
#if DEBUG_RESOLVE
        str node_type_str = tl_polytype_to_string(self->transient, node->type);
        fprintf(stderr, "constrain_or_set: '%s' : %s :: %s\n", str_cstr(&name), str_cstr(&node_type_str),
                str_cstr(&poly_str));
#endif
        if (constrain(self, node->type, type, node, TL_UNIFY_SYMMETRIC)) return type_error(self, node);
    }

    else {
#if DEBUG_RESOLVE
        fprintf(stderr, "constrain_or_set: '%s': %s\n", str_cstr(&name), str_cstr(&poly_str));
#endif
        ast_node_type_set(node, type);
    }
    return 0;
}

int expected_symbol(tl_infer *self, ast_node const *node) {
    array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_symbol, .node = node}));
    return 1;
}

void sync_with_env(tl_infer *self, traverse_ctx *ctx, ast_node *node, int want_fresh) {
    // If it's a symbol, if it has a type, add it to the environment. If it doesn't have a type, look it up
    // from the environment.
    if (!ast_node_is_symbol(node)) goto finish;

    str name = ast_node_str(node);
    if (ctx && str_map_contains(ctx->type_arguments, name)) goto finish;

    if (want_fresh && !node->type) {
        node->type = tl_polytype_create_fresh_tv(self->arena, self->subs);
    }

    if (node->type) {
        // A symbol with an existing type: either insert it to the environment, or constrain it with the
        // existing type if already in env.
        env_insert_constrain(self, name, node->type, node);
    } else {
        // No type: look up its type from the environment, if any
        tl_polytype *type = tl_type_env_lookup(self->env, name);
#if DEBUG_TYPE_ALIAS
        if (!type) {
            int is_alias = tl_type_registry_is_type_alias(self->registry, name);
            fprintf(stderr, "[DEBUG_TYPE_ALIAS] sync_with_env: '%s' not in type env\n", str_cstr(&name));
            if (is_alias) fprintf(stderr, "[DEBUG_TYPE_ALIAS]   BUT exists in registry as type alias!\n");
        }
#endif
        if (type) ast_node_type_set(node, type);
    }

finish:
    // regardless, node must have a type, even if it's a fresh tv
    if (!node->type) node->type = tl_polytype_create_fresh_tv(self->arena, self->subs);
}

int check_is_pointer(tl_infer *self, tl_polytype *type, ast_node *node) {
    if (tl_monotype_is_inst(type->type)) {
        if (!tl_monotype_is_ptr(type->type)) {
            array_push(self->errors, ((tl_infer_error){.tag = tl_err_expected_pointer, .node = node}));
            return 1;
        }
    }
    return 0;
}

void ensure_symbol_type_from_env(tl_infer *self, ast_node *node) {
    if (!ast_node_is_symbol(node)) return;

    tl_polytype *poly = tl_type_env_lookup(self->env, ast_node_str(node));

#if DEBUG_TYPE_ALIAS
    {
        str name_dbg = ast_node_str(node);
        if (tl_type_registry_is_type_alias(self->registry, name_dbg)) {
            char const *type_s = "(null)";
            char const *node_s = "(null)";
            str         type_dbg, node_dbg;
            if (poly) {
                type_dbg = tl_polytype_to_string(self->transient, poly);
                type_s   = str_cstr(&type_dbg);
            }
            if (node->type) {
                node_dbg = tl_polytype_to_string(self->transient, node->type);
                node_s   = str_cstr(&node_dbg);
            }
            fprintf(stderr,
                    "[DEBUG_TYPE_ALIAS] ensure_symbol_type_from_env: '%s' env_lookup=%s node_type=%s\n",
                    str_cstr(&name_dbg), type_s, node_s);
        }
    }
#endif

    // Note: do not override node->type if it is already concrete. There is some confusion with the handling
    // of type literals that makes the constrain fail unless it is guarded behind this if condition.
    if (!node->type || !tl_polytype_is_concrete(node->type)) constrain_or_set(self, node, poly);
}

static int infer_struct_access(tl_infer *self, traverse_ctx *ctx, ast_node *node) {
    if (!ast_node_is_binary_op_struct_access(node)) fatal("logic error");
    ensure_tv(self, &node->type);
    ensure_tv(self, &node->binary_op.left->type);
    ensure_tv(self, &node->binary_op.right->type);
    ast_node    *left = node->binary_op.left, *right = node->binary_op.right;
    char const  *op          = str_cstr(&node->binary_op.op->symbol.name);

    tl_monotype *struct_type = null;

    // handle -> vs . access
    if (0 == strcmp("->", op)) {

        // FIXME: it should be an error if inference completes and struct access has never checked
        // field names being valid. Possibly do this check in a later phase rather than here.

        // if type is not a constructor instance, all we can assert is that the left side must be a
        // pointer
        if (check_is_pointer(self, left->type, left)) return 1;
        ensure_symbol_type_from_env(self, left);

        if (tl_monotype_is_ptr(left->type->type)) {
            struct_type = tl_monotype_ptr_target(left->type->type);
        } else {
            tl_monotype *weak = tl_monotype_create_fresh_weak(self->subs);
            tl_monotype *ptr  = tl_type_registry_ptr(self->registry, weak);
            if (constrain_pm(self, left->type, ptr, node, TL_UNIFY_SYMMETRIC)) return 1;
            struct_type = weak;
        }

    } else {
        ensure_symbol_type_from_env(self, left);
        struct_type = (tl_monotype *)left->type->type;
#if DEBUG_TYPE_ALIAS
        if (ast_node_is_symbol(left)) {
            str left_name = ast_node_str(left);
            if (tl_type_registry_is_type_alias(self->registry, left_name)) {
                str st_str = tl_monotype_to_string(self->transient, struct_type);
                fprintf(stderr,
                        "[DEBUG_TYPE_ALIAS] infer_struct_access: left='%s' struct_type=%s is_inst=%d\n",
                        str_cstr(&left_name), str_cstr(&st_str), tl_monotype_is_inst(struct_type));
            }
        }
#endif
    }

    // Note: must substitute to resolve type of chained field access, eg: foo.bar.baz
    tl_monotype_substitute(self->arena, struct_type, self->subs, null); // needed

    // Const(T) is transparent for field access: unwrap to access T's fields
    if (tl_monotype_is_const(struct_type)) {
        struct_type = tl_monotype_const_target(struct_type);
    }

#if DEBUG_TYPE_ALIAS
    if (ast_node_is_symbol(left) && tl_type_registry_is_type_alias(self->registry, ast_node_str(left))) {
        str st_str2 = tl_monotype_to_string(self->transient, struct_type);
        fprintf(stderr, "[DEBUG_TYPE_ALIAS] infer_struct_access: after subst, struct_type=%s is_inst=%d\n",
                str_cstr(&st_str2), tl_monotype_is_inst(struct_type));
    }
#endif

    if (tl_monotype_is_inst(struct_type)) {
        // Note: this handling of nfas supports terms like: `obj.fun_ptr()` where a field called
        // fun_ptr is a function pointer.
        ast_node *nfa = null;
        if (ast_node_is_nfa(right)) {
            nfa   = right;
            right = right->named_application.name;
        }
        ensure_tv(self, &right->type);
        if (ast_node_is_symbol(right)) {
            str                       field_name = right->symbol.name;
            tl_type_constructor_inst *inst       = struct_type->cons_inst;
            i32 found = tl_monotype_type_constructor_field_index(struct_type, field_name);

            dbg(self, "searched struct '%s' field name %s", str_cstr(&inst->def->name),
                str_cstr(&field_name));

            if (found != -1) {
                if (!inst->args.size) {
                    // empty struct
                    if (constrain_pm(self, right->type, struct_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    if (constrain_pm(self, node->type, struct_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    if (constrain(self, node->type, right->type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    goto end_struct_access_op;
                }

                if ((u32)found >= inst->args.size) fatal("out of range");
                tl_monotype *field_type = inst->args.v[found];
                if (nfa) {
                    tl_monotype *result_type;
                    if (tl_monotype_is_arrow(field_type)) {
                        result_type = tl_monotype_arrow_result(field_type);
                    } else {
                        // Field type is not yet an arrow (e.g., a type variable).
                        result_type = tl_monotype_create_fresh_tv(self->subs);
                    }
                    // right = nfa's name
                    if (constrain_pm(self, right->type, field_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    if (constrain_pm(self, nfa->type, result_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    if (constrain_pm(self, node->type, result_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                } else {
                    if (constrain_pm(self, right->type, field_type, node, TL_UNIFY_SYMMETRIC)) return 1;

                    if (tl_monotype_is_carray(field_type)) {
                        tl_monotype *target   = tl_monotype_carray_element(field_type);
                        tl_monotype *ptr_type = tl_type_registry_ptr(self->registry, target);
                        if (constrain_pm(self, node->type, ptr_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    } else {
                        if (constrain_pm(self, node->type, field_type, node, TL_UNIFY_SYMMETRIC)) return 1;
                        if (constrain(self, node->type, right->type, node, TL_UNIFY_SYMMETRIC)) return 1;
                    }
                }
            } else if (nfa) {
                // UFCS: field not found, but right side is a function call.
                // Rewrite x.foo(a, b) to foo(x, a, b).
                u8  ufcs_arity = nfa->named_application.n_arguments + 1;
                str ufcs_name  = mangle_str_for_arity(self->arena, field_name, ufcs_arity);
                if (!tl_type_env_lookup(self->env, ufcs_name) &&
                    !tl_type_registry_get(self->registry, ufcs_name)) {
                    array_push(self->errors,
                               ((tl_infer_error){.tag = tl_err_field_not_found, .node = right}));
                    return 1;
                }

                // Prepend `left` to the NFA's argument list
                u8         old_n    = nfa->named_application.n_arguments;
                ast_node **new_args = alloc_malloc(self->arena, (old_n + 1) * sizeof(ast_node *));
                new_args[0]         = left;
                for (u8 i = 0; i < old_n; i++) new_args[i + 1] = nfa->named_application.arguments[i];

                // Rewrite node in place from binary_op to NFA
                ast_node_name_replace(nfa->named_application.name, ufcs_name);
                node->tag                                   = nfa->tag;
                node->named_application.name                = nfa->named_application.name;
                node->named_application.arguments           = new_args;
                node->named_application.n_arguments         = ufcs_arity;
                node->named_application.type_arguments      = null;
                node->named_application.n_type_arguments    = 0;
                node->named_application.is_specialized        = 0;
                node->named_application.is_type_constructor   = 0;
                node->named_application.is_function_reference = 0;

                return infer_named_function_application(self, ctx, node);
            } else {
                array_push(self->errors, ((tl_infer_error){.tag = tl_err_field_not_found, .node = right}));
                return 1;
            }

        } else {
            // not a symbol
            fatal("unreachable");
        }
    }

    else {
        // struct type is not a type constructor
        dbg(self, "warning: infer struct access without a struct type");
#if DEBUG_TYPE_ALIAS
        if (ast_node_is_symbol(left)) {
            str left_name2 = ast_node_str(left);
            int is_alias2  = tl_type_registry_is_type_alias(self->registry, left_name2);
            str st_str3    = tl_monotype_to_string(self->transient, struct_type);
            fprintf(
              stderr,
              "[DEBUG_TYPE_ALIAS] infer_struct_access: FALLTHROUGH left='%s' struct_type=%s is_alias=%d\n",
              str_cstr(&left_name2), str_cstr(&st_str3), is_alias2);
            if (is_alias2) {
                tl_polytype *alias_poly = tl_type_registry_get(self->registry, left_name2);
                if (alias_poly) {
                    str alias_str = tl_polytype_to_string(self->transient, alias_poly);
                    fprintf(stderr, "[DEBUG_TYPE_ALIAS]   alias points to: %s\n", str_cstr(&alias_str));
                }
            }
        }
#endif
    }

end_struct_access_op:
    // always substitute operands immediately
    return 0;
}

static void maybe_handle_null(tl_infer *self, ast_node *node) {
    // Note: special case: if `null` appears and there is no node type yet, or if it's not a Ptr, assign a
    // Ptr(tv). The reason we do this is to assist non-annotated nodes such as struct fields that are
    // initialised to null. Without this handling, Foo(ptr = Null) would need to be Foo(ptr: Ptr(T) = null).
    //
    // Note: special case: if `void` appears, we assign a fresh type variable. The transpiler will detect
    // void nodes and leave the struct field uninitialised.
    //
    // Only relevant for value positions (npos_function_argument, npos_operand, npos_value_rhs), not for
    // name/definition positions (npos_toplevel, npos_formal_parameter, npos_assign_lhs, etc.).

    if (ast_node_is_nil(node) || ast_node_is_void(node)) {
        if (!node->type || !tl_monotype_is_ptr(node->type->type)) {
            ast_node_type_set(node, tl_polytype_create_fresh_tv(self->arena, self->subs));
        }
    }
}

int resolve_node(tl_infer *self, ast_node *node, traverse_ctx *ctx, node_position pos) {
    // Note: ctx may be null if this processing is initiated from load_toplevel()

    if (!node) return 0;

    switch (pos) {

    case npos_toplevel:
    case npos_formal_parameter:
        if (!ast_node_is_symbol(node)) {
#if DEBUG_RESOLVE
            fprintf(stderr, "[DEBUG resolve_node] ERROR: npos_toplevel/formal_parameter expected symbol\n");
#endif
            return expected_symbol(self, node);
        }

        if (process_annotation(self, ctx, node, (annotation_opts){.check_type_arg_self = 1}) < 0) {
#if DEBUG_RESOLVE
            fprintf(stderr,
                    "[DEBUG resolve_node] ERROR: npos_toplevel/formal_parameter process_annotation "
                    "failed for '%s'\n",
                    str_cstr(&node->symbol.name));
#endif
            return 1;
        }

        if (ctx) {
            // Add the symbol's own name to lexical_names (distinct from type args added above)
            str_hset_insert(&ctx->lexical_names, node->symbol.name);
        }

        // Sync with existing symbol, if any.
        sync_with_env(self, ctx, node, 0);
        break;

    case npos_function_argument:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, ctx, node);

        maybe_handle_null(self, node);

        // Only symbols/nfas need env sync here. Complex expression arguments (calls, literals, etc.)
        // are handled by their own infer_* handlers during traversal.
        if (ast_node_is_symbol(node) || ast_node_is_nfa(node)) {
            if (!ctx) fatal("logic error");
            sync_with_env(self, ctx, node, 1);
        }

        break;

    case npos_assign_lhs:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, ctx, node);

        // After struct access is handled above, only symbols should remain as assign LHS nodes.
        if (!ast_node_is_symbol(node)) break;
        if (!ctx) fatal("logic error");

        // Parse the annotation if present (no special opts)
        if (process_annotation(self, ctx, node, (annotation_opts){0}) < 0) {
#if DEBUG_RESOLVE
            fprintf(stderr,
                    "[DEBUG resolve_node] ERROR: npos_assign_lhs process_annotation failed for '%s'\n",
                    str_cstr(&node->symbol.name));
#endif
            return 1;
        }
        ensure_tv(self, &node->type);
        break;

    case npos_reassign_lhs:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, ctx, node);

        // Take symbol's existing type: this ensures let-in symbols retain their type info through
        // subsequent mutations. (sync_with_env is safe with ctx==null.)
        sync_with_env(self, ctx, node, 0);
        break;

    case npos_field_name:
        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, ctx, node);

        // assign a fresh type variable to field name, because we can't know its generic instantiated type
        ensure_tv(self, &node->type);
        break;

    case npos_operand:
        if (!ctx) fatal("logic error");

        if (ast_node_is_binary_op_struct_access(node)) return infer_struct_access(self, ctx, node);

        maybe_handle_null(self, node);
        sync_with_env(self, ctx, node, 0);
        break;

    case npos_value_rhs:
        maybe_handle_null(self, node);

        // Sync with env, creating a fresh type variable if the node has no type yet (want_fresh=1).
        // Safe with ctx==null (called from load_toplevel for let-in values).
        sync_with_env(self, ctx, node, 1);
        break;
    }

#if DEBUG_RESOLVE
    {
        str node_str = v2_ast_node_to_string(self->transient, node);
        str type_str = node->type ? tl_polytype_to_string(self->transient, node->type) : S("(null)");
        dbg(self, "resolve_node pos %i:  %s : %s", pos, str_cstr(&node_str), str_cstr(&type_str));
    }
#endif

    return 0;
}

// ============================================================================
// Type predicate checking
// ============================================================================

int check_type_predicate(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {

    // Note: special case: using type predicate operator `::` as an attribute predicate
    if (ast_attribute_set == node->type_predicate.rhs->tag) {
        if (!ast_node_is_symbol(node->type_predicate.lhs)) {
            return expected_symbol(self, node->type_predicate.lhs);
        }
        // lookup symbol attributes in type environment
        ast_node *sym_attributes = str_map_get_ptr(self->attributes, node->type_predicate.lhs->symbol.name);

        ast_node **want_attributes   = node->type_predicate.rhs->attribute_set.nodes;
        u8         want_attributes_n = node->type_predicate.rhs->attribute_set.n;

        int        found_all         = 1; // default true
        for (u8 i = 0; i < want_attributes_n; i++) {
            ast_node *want      = want_attributes[i];
            u64       want_hash = ast_node_hash(want);
            if (0 == want_hash) fatal("runtime error"); // 0 hash illegal and breaks logic here

            int found_one = 0;
            if (sym_attributes && ast_attribute_set == sym_attributes->tag) {
                for (u8 j = 0; j < sym_attributes->attribute_set.n; j++) {
                    ast_node *one           = sym_attributes->attribute_set.nodes[j];
                    u64       has_hash      = ast_node_hash(one);
                    u64       let_name_hash = 0;

                    // also support general match of NFA names, e.g. `[[nfa(123)]] foo := 1 foo :: [[nfa]]`
                    if (ast_node_is_nfa(one)) let_name_hash = ast_node_hash(one->let.name);

                    if (has_hash == want_hash || (let_name_hash == want_hash)) {
                        found_one = 1;
                        break;
                    }
                }
            }
            if (!found_one) {
                found_all = 0;
                break;
            }
        }

        node->type_predicate.is_valid = found_all;
        return 0;
    }

    // Check if LHS is a type argument (pattern: T :: ConcreteType)
    if (ast_node_is_symbol(node->type_predicate.lhs)) {
        str          lhs_name     = ast_node_str(node->type_predicate.lhs);
        tl_monotype *lhs_type_arg = str_map_get_ptr(traverse_ctx->type_arguments, lhs_name);

        if (lhs_type_arg) {
            // LHS is a type argument - handle it specially
            tl_monotype *rhs_type = parse_type_arg(self, traverse_ctx->type_arguments,
                                                    node->type_predicate.rhs);

            tl_monotype *lhs_mono     = lhs_type_arg;

            if (!tl_monotype_is_concrete(lhs_mono)) {
                tl_monotype_substitute(self->arena, lhs_mono, self->subs, null);
                if (!tl_monotype_is_concrete(lhs_mono)) {
                    log_subs(self);
                    return unresolved_type_error(self, node->type_predicate.lhs);
                }
            }

            // Compare types using constrain with error suppression
            int save                        = self->is_constrain_ignore_error;
            self->is_constrain_ignore_error = 1;

            tl_polytype *lhs_poly           = tl_polytype_absorb_mono(self->arena, lhs_mono);
            if (!rhs_type || constrain_pm(self, lhs_poly, rhs_type, node, TL_UNIFY_SYMMETRIC)) {
                node->type_predicate.is_valid = 0;
            } else {
                node->type_predicate.is_valid = 1;
            }

            self->is_constrain_ignore_error = save;
            ast_node_type_set(node, tl_polytype_bool(self->arena, self->registry));
            return 0;
        }
    }
    // Fall through to existing expression handling...

    tl_monotype *type = parse_type_arg(self, traverse_ctx->type_arguments,
                                       node->type_predicate.rhs);

    if (resolve_node(self, node->type_predicate.lhs, traverse_ctx, npos_operand)) {
        dbg(self, "assert resolve node failed");
        return 1;
    }
    tl_polytype *name_type = node->type_predicate.lhs->type;
    if (!tl_polytype_is_concrete(name_type)) {
        tl_polytype_substitute(self->arena, name_type, self->subs);
        if (!tl_polytype_is_concrete(name_type)) {
            log_subs(self);
            return unresolved_type_error(self, node->type_predicate.lhs);
        }
    }

    // Rather than generate a type error during compilation, we now treat the node as a boolean. Set flag to
    // ignore constraint errors.
    {
        int save                        = self->is_constrain_ignore_error;
        self->is_constrain_ignore_error = 1;

        if (!type || constrain_pm(self, node->type_predicate.lhs->type, type, node, TL_UNIFY_SYMMETRIC)) {
            node->type_predicate.is_valid = 0;
        } else {
            node->type_predicate.is_valid = 1;
        }

        self->is_constrain_ignore_error = save;
    }

    ast_node_type_set(node, tl_polytype_bool(self->arena, self->registry));
    return 0;
}

// ============================================================================
// Inference dispatch (infer_traverse_cb)
// ============================================================================
//
// Main callback for Phase 3: dispatches each AST node to the appropriate
// infer_*() handler during bottom-up type inference.

int infer_traverse_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (null == node) return 0;

#if DEBUG_RESOLVE
    str node_str = v2_ast_node_to_string(self->transient, node);
    fprintf(stderr, "infer_traverse_cb: %s:  %s\n", ast_tag_to_string(node->tag), str_cstr(&node_str));
#endif

    switch (node->tag) {

    case ast_attribute_set:
        // not traversed
        break;

    case ast_type_predicate: return infer_literal_type(self, node, tl_type_registry_bool);

    case ast_nil:            return infer_nil(self, node);

    case ast_void:
        // else handled by maybe_handle_null()
        return infer_void(self, traverse_ctx, node);

    case ast_string:    return infer_literal_type(self, node, tl_type_registry_ptr_char);
    case ast_char:      return infer_literal_type(self, node, tl_type_registry_char);
    case ast_f64:       return infer_weak_float_literal(self, node);
    case ast_i64:       return infer_weak_int_literal(self, node, 1);
    case ast_i64_z:     return infer_literal_type(self, node, tl_type_registry_cptrdiff);
    case ast_u64:       return infer_weak_int_literal(self, node, 0);
    case ast_u64_zu:    return infer_literal_type(self, node, tl_type_registry_csize);
    case ast_bool:      return infer_literal_type(self, node, tl_type_registry_bool);
    case ast_body:      return infer_body(self, node);
    case ast_case:      return infer_case(self, traverse_ctx, node);
    case ast_return:    return infer_return(self, traverse_ctx, node);
    case ast_try:       return infer_try(self, traverse_ctx, node);

    case ast_binary_op: return infer_binary_op(self, traverse_ctx, node);
    case ast_unary_op:  return infer_unary_op(self, traverse_ctx, node);
    case ast_let_in:    return infer_let_in(self, traverse_ctx, node);

    case ast_symbol:
        // When resolving a symbol, we need to know what context it's in. This is specified by its parent
        // node, and is communicated via the `node_pos` field in traverse_ctx.
        if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;
        assert(node->type);
        break;

    case ast_named_function_application: return infer_named_function_application(self, traverse_ctx, node);
    case ast_lambda_function_application:
        return infer_lambda_function_application(self, traverse_ctx, node);
    case ast_lambda_function:      return infer_lambda_function(self, traverse_ctx, node);
    case ast_if_then_else:         return infer_if_then_else(self, node);

    case ast_tuple:                return infer_tuple(self, node);

    case ast_trait_definition:     break;
    case ast_user_type_definition: break;

    case ast_assignment:           return infer_assignment(self, traverse_ctx, node);

    case ast_reassignment:
    case ast_reassignment_op:      return infer_reassignment(self, traverse_ctx, node);

    case ast_continue:
        // use 'any' for continue so it can unify with any other conditional arm
        return infer_continue(self, node);

    case ast_while:        return infer_while(self, node);

    case ast_let:          // intentionally not processed
    case ast_hash_command:
    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_type_alias:   break;
    }

    return 0;
}
