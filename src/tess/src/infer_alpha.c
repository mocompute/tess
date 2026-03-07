// infer_alpha.c — Phase 1: Alpha Conversion
//
// Gives every bound variable a globally unique name (e.g. x -> x_v3) to
// eliminate shadowing before inference.  Also handles free variable collection
// and concretize_params for parameter type assignment during specialization.

#include "infer_internal.h"

// ============================================================================
// Alpha conversion
// ============================================================================

void rename_let_in(tl_infer *self, ast_node *node, rename_variables_ctx *ctx) {
    // For toplevel definitions, rename them and keep them in lexical scope.
    if (!ast_node_is_let_in(node)) return;

    str name = node->let_in.name->symbol.name;
    if (is_c_symbol(name)) return;

    str newvar = next_variable_name(self, name);
    ast_node_name_replace(node->let_in.name, newvar);

#if DEBUG_RENAME
    dbg(self, "rename %.*s => %.*s", str_ilen(node->let_in.name->symbol.original),
        str_buf(&node->let_in.name->symbol.original), str_ilen(node->let_in.name->symbol.name),
        str_buf(&node->let_in.name->symbol.name));
#endif

    str_map_set(&ctx->lex, name, &newvar);
}

static void rename_one_function_param(tl_infer *self, ast_node *param, rename_variables_ctx *ctx,
                                      int level) {
    if (ast_node_is_nfa(param)) {
        // T[a] vs T[Int] -- what to do?
        rename_one_function_param(self, param->named_application.name, ctx, level + 1);

        u32        argc = param->named_application.n_type_arguments;
        ast_node **argv = param->named_application.type_arguments;
        for (u32 i = 0; i < argc; i++) {
            rename_one_function_param(self, argv[i], ctx, level + 1);
        }

    } else if (ast_node_is_symbol(param)) {

        ast_node_type_set(param, null);

        str *found;

        if ((found = str_map_get(ctx->lex, param->symbol.name))) {
            ast_node_name_replace(param, *found);
#if DEBUG_RENAME
            fprintf(stderr, "rename %.*s => %.*s\n", str_ilen(param->symbol.original),
                    str_buf(&param->symbol.original), str_ilen(param->symbol.name),
                    str_buf(&param->symbol.name));
#endif
        } else if (param->symbol.is_mangled && (found = str_map_get(ctx->lex, param->symbol.original))) {
            // name was mangled because it conflicts with a toplevel name. But lexical rename is meant
            // to take precedence over mangling to match toplevel names.
            ast_node_name_replace(param, *found);
#if DEBUG_RENAME
            fprintf(stderr, "rename mangled %.*s => %.*s\n", str_ilen(param->symbol.original),
                    str_buf(&param->symbol.original), str_ilen(param->symbol.name),
                    str_buf(&param->symbol.name));
#endif
        } else {
            // a param or type argument seen for the first time: add renamed var to lexical scope

            str name   = param->symbol.name;
            str newvar = next_variable_name(self, name);
            ast_node_name_replace(param, newvar);
            str_map_set(&ctx->lex, name, &newvar);
            rename_variables(self, param, ctx, level + 1);

#if DEBUG_RENAME
            fprintf(stderr, "rename new %s => %s\n", str_cstr(&name), str_cstr(&newvar));
#endif
        }
    }
}

static hashmap *rename_function_params(tl_infer *self, ast_node *node, rename_variables_ctx *ctx,
                                       int level) {
    hashmap *save = map_copy(ctx->lex);

    // alpha conversion of type arguments
    if (ast_node_is_let(node)) {
        u32        argc = node->let.n_type_parameters;
        ast_node **argv = node->let.type_parameters;
        for (u32 i = 0; i < argc; i++) {
            rename_one_function_param(self, argv[i], ctx, level);
        }
    }

    ast_arguments_iter iter = ast_node_arguments_iter(node);
    ast_node          *param;
    while ((param = ast_arguments_next(&iter))) {
        rename_one_function_param(self, param, ctx, level);
    }

    return save;
}

// -- rename_variables (main recursive body) --

static void rename_case_variables(tl_infer *self, ast_node *node, rename_variables_ctx *ctx, int level) {
    int is_union = node->case_.is_union;

    rename_variables(self, node->case_.expression, ctx, level + 1);
    rename_variables(self, node->case_.binary_predicate, ctx, level + 1);
    rename_variables(self, node->case_.union_annotation, ctx, level + 1);
    if (node->case_.conditions.size != node->case_.arms.size) fatal("runtime error");
    forall(i, node->case_.conditions) {
        hashmap *save = null;
        if (is_union && ast_node_is_symbol(node->case_.conditions.v[i])) {
            // node may be ast_nil for an else clause
            str name   = ast_node_str(node->case_.conditions.v[i]);
            str newvar = next_variable_name(self, name);

            // establish lexical scope of the union case binding
            save = map_copy(ctx->lex);
            str_map_set(&ctx->lex, name, &newvar);
        }

        rename_variables(self, node->case_.conditions.v[i], ctx, level + 1);
        rename_variables(self, node->case_.arms.v[i], ctx, level + 1);

        if (save) {
            map_destroy(&ctx->lex);
            ctx->lex = save;
        }
    }
}

void rename_variables(tl_infer *self, ast_node *node, rename_variables_ctx *ctx, int level) {
    // level should be 0 on entry. It is used to recognize toplevel let nodes which assign static values
    // that must remain in lexical scope throughout the program.

    if (null == node) return;

    // ensure all types are removed: important for the post-clone rename of functions being specialized.
    ast_node_type_set(node, null);

    // also clear types attached to any explicit type arguments
    {
        u32        argc = 0;
        ast_node **argv = null;
        if (ast_node_is_let(node)) {
            argc = node->let.n_type_parameters;
            argv = node->let.type_parameters;
        } else if (ast_node_is_nfa(node)) {
            argc = node->named_application.n_type_arguments;
            argv = node->named_application.type_arguments;
        } else if (ast_node_is_utd(node)) {
            argc = node->user_type_def.n_type_arguments;
            argv = node->user_type_def.type_arguments;
        }

        for (u32 i = 0; i < argc; i++) ast_node_type_set(argv[i], null);
    }

    switch (node->tag) {

    case ast_if_then_else:
        rename_variables(self, node->if_then_else.condition, ctx, level + 1);
        rename_variables(self, node->if_then_else.yes, ctx, level + 1);
        rename_variables(self, node->if_then_else.no, ctx, level + 1);
        break;

    case ast_let_in: {

        // recurse on value prior to adding name to lexical scope
        rename_variables(self, node->let_in.value, ctx, level + 1);

        hashmap *save = null;
        str      name = node->let_in.name->symbol.name;
        if (is_c_symbol(name)) break;
        if (level) {
            // do not rename toplevel symbols again (see rename_let_in)
            str newvar = next_variable_name(self, name);

            // establish lexical scope of the let-in binding and recurse
            save = map_copy(ctx->lex);
            str_map_set(&ctx->lex, name, &newvar);

            rename_variables(self, node->let_in.name, ctx, level + 1);
        }

        rename_variables(self, node->let_in.body, ctx, level + 1);

        // restore prior scope
        if (save) {
            map_destroy(&ctx->lex);
            ctx->lex = save;
        }
    } break;

    case ast_symbol: {

        str *found;
        if (!ctx->is_field) {
            // Do not rename symbols found immediately after a struct access
            if ((found = str_map_get(ctx->lex, node->symbol.name))) {
                ast_node_name_replace(node, *found);
#if DEBUG_RENAME
                dbg(self, "rename %.*s => %.*s", str_ilen(node->symbol.original),
                    str_buf(&node->symbol.original), str_ilen(node->symbol.name),
                    str_buf(&node->symbol.name));
#endif
            } else if (node->symbol.is_mangled && (found = str_map_get(ctx->lex, node->symbol.original))) {
                // name was mangled because it conflicts with a toplevel name. But lexical rename is meant
                // to take precedence over mangling to match toplevel names.
                ast_node_name_replace(node, *found);
#if DEBUG_RENAME
                dbg(self, "rename mangled %.*s => %.*s", str_ilen(node->symbol.original),
                    str_buf(&node->symbol.original), str_ilen(node->symbol.name),
                    str_buf(&node->symbol.name));
#endif
            } else {
                // a free variable, a field name, a toplevel function name, etc
            }
        }

        // No matter what, reset field_name processing from this point forward
        ctx->is_field = 0;

        // ensure renamed symbols do not carry a type
        ast_node_type_set(node, null);
        node->symbol.annotation_type = null;

        // traverse into annotation too, to support type arguments.
        // Note: keep in sync with ast_let_in arm.
        rename_variables(self, node->symbol.annotation, ctx, level + 1);
    } break;

    case ast_lambda_function: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->lambda_function.body, ctx, level + 1);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_let: {
        hashmap *save = rename_function_params(self, node, ctx, level);
        rename_variables(self, node->let.body, ctx, level + 1);
        ast_node_type_set(node->let.name, null);
        map_destroy(&ctx->lex);
        ctx->lex = save;
    } break;

    case ast_lambda_function_application: {
        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;
        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

        // establishes scope for lambda body
        rename_variables(self, node->lambda_application.lambda, ctx, level + 1);

    } break;

    case ast_named_function_application: {
        rename_variables(self, node->named_application.name, ctx, level + 1);

        // type arguments
        u32        argc = node->named_application.n_type_arguments;
        ast_node **argv = node->named_application.type_arguments;
        for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);

        ast_arguments_iter iter = ast_node_arguments_iter(node);
        ast_node          *arg;

        while ((arg = ast_arguments_next(&iter))) rename_variables(self, arg, ctx, level + 1);

    } break;

    case ast_trait_definition: {
        // type arguments
        u32        argc = node->trait_def.n_type_arguments;
        ast_node **argv = node->trait_def.type_arguments;
        for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);

        // traverse into signatures
        for (u32 i = 0; i < node->trait_def.n_signatures; i++)
            rename_variables(self, node->trait_def.signatures[i], ctx, level + 1);

        // traverse into parent references
        for (u32 i = 0; i < node->trait_def.n_parents; i++)
            rename_variables(self, node->trait_def.parents[i], ctx, level + 1);

    } break;

    case ast_user_type_definition: {
        // type arguments
        u32        argc = node->user_type_def.n_type_arguments;
        ast_node **argv = node->user_type_def.type_arguments;
        for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);

        // traverse into field annotations
        if (node->user_type_def.field_annotations) { // may be null for enums
            argc = node->user_type_def.n_fields;
            argv = node->user_type_def.field_annotations;
            for (u32 i = 0; i < argc; i++) rename_variables(self, argv[i], ctx, level + 1);
        }

    } break;

    case ast_tuple: {
        ast_node_sized arr = ast_node_sized_from_ast_array(node);
        forall(i, arr) rename_variables(self, arr.v[i], ctx, level + 1);
    } break;

    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:
        // Note: no longer rename lhs of assignment, because it is used for named arguments of type
        // constructors. However, the type must be erased, because cloning generic functions relies on
        // rename_variables to erase types.
        if (!node->assignment.is_field_name) rename_variables(self, node->assignment.name, ctx, level + 1);
        else {
            // Note: however, field names may now be annotated, so the annotations have to be processed.
            // This handles type variables in the field assignment annotation.
            if (ast_node_is_symbol(node->assignment.name))
                rename_variables(self, node->assignment.name->symbol.annotation, ctx, level + 1);

            ast_node_type_set(node->assignment.name, null);
        }

        rename_variables(self, node->assignment.value, ctx, level + 1);
        break;

    case ast_binary_op: {
        rename_variables(self, node->binary_op.left, ctx, level + 1);

        // Note: If op is a struct access operator (. or ->), signal it
        char const *op = str_cstr(&node->binary_op.op->symbol.name);
        if (is_struct_access_operator(op)) ctx->is_field = 1;
        rename_variables(self, node->binary_op.right, ctx, level + 1);
    } break;

    case ast_unary_op: rename_variables(self, node->unary_op.operand, ctx, level + 1); break;

    case ast_return:   rename_variables(self, node->return_.value, ctx, level + 1); break;

    case ast_try:      rename_variables(self, node->try_.operand, ctx, level + 1); break;

    case ast_while:
        rename_variables(self, node->while_.condition, ctx, level + 1);
        rename_variables(self, node->while_.update, ctx, level + 1);
        rename_variables(self, node->while_.body, ctx, level + 1);
        break;

    case ast_body:

        forall(i, node->body.expressions) {
            rename_variables(self, node->body.expressions.v[i], ctx, level + 1);
        }
        forall(i, node->body.defers) {
            rename_variables(self, node->body.defers.v[i], ctx, level + 1);
        }
        break;

    case ast_case: rename_case_variables(self, node, ctx, level); break;

    case ast_type_predicate:
        //
        rename_variables(self, node->type_predicate.lhs, ctx, level + 1);
        // Also rename type argument references in RHS (e.g., T in "x :: T")
        rename_variables(self, node->type_predicate.rhs, ctx, level + 1);
        break;

    case ast_attribute_set:
    case ast_hash_command:
    case ast_continue:
    case ast_string:
    case ast_char:
    case ast_nil:
    case ast_void:
    case ast_arrow:
    case ast_bool:
    case ast_ellipsis:
    case ast_eof:
    case ast_f64:
    case ast_i64:
    case ast_i64_z:
    case ast_u64:
    case ast_u64_zu:
    case ast_type_alias:    break;
    }
}

// ============================================================================
// Concretize parameters
// ============================================================================

void concretize_params(tl_infer *self, ast_node *node, tl_monotype *callsite, hashmap *type_arguments,
                       tl_monotype_sized resolved_type_args) {
    if (ast_node_is_symbol(node)) return;

    ast_node      *body   = null;
    ast_node_sized params = {0};
    if (ast_node_is_let(node)) {
        body   = node->let.body;
        params = ast_node_sized_from_ast_array(node);
    } else if (ast_node_is_let_in_lambda(node)) {
        body   = node->let_in.value->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->let_in.value);
    } else if (ast_node_is_lambda_application(node)) {
        body   = node->lambda_application.lambda->lambda_function.body;
        params = ast_node_sized_from_ast_array(node->lambda_application.lambda);
    } else {
        fatal("logic error");
    }

    // assign concrete types to parameters based on callsite arguments

    assert(tl_arrow == callsite->tag);
    assert(callsite->list.xs.size == 2);
    assert(tl_tuple == callsite->list.xs.v[0]->tag);
    tl_monotype_sized callsite_args = callsite->list.xs.v[0]->list.xs;
    assert(callsite_args.size == params.size);

    forall(i, params) {
        ast_node    *param         = params.v[i];
        tl_polytype *callsite_type = tl_polytype_absorb_mono(self->arena, callsite_args.v[i]);
        if (!ast_node_is_symbol(param)) fatal("runtime error");
        ast_node_type_set(param, callsite_type);

        // this ensures the environment is also updated, since symbol types are in the env
        env_insert_constrain(self, ast_node_str(param), callsite_type, param);

        // Force-update the env entry to the concrete callsite type.  env_insert_constrain only
        // constrains (unifies) when the name already exists, which keeps the old type-variable
        // entry and loses metadata such as free-variable lists.  Overwriting ensures the env
        // carries the full callsite type including free variables.
        tl_type_env_insert(self->env, ast_node_str(param), callsite_type);
    }

    // assign concrete types to type parameters based on explicit type arguments from callsite
    if (type_arguments && ast_node_is_let(node)) {
#if DEBUG_EXPLICIT_TYPE_ARGS
        fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] node has %u type params, type_arguments=%p\n",
                node->let.n_type_parameters, (void *)type_arguments);
#endif
        for (u32 i = 0; i < node->let.n_type_parameters; i++) {
            ast_node *type_param = node->let.type_parameters[i];
            assert(ast_node_is_symbol(type_param));

            // Always use the alpha-converted name, not the original, because the type
            // environment relies on alpha conversion to prevent pollution between generic
            // and specialized phases.
            str          param_name = type_param->symbol.name;
            tl_monotype *bound_type = str_map_get_ptr(type_arguments, param_name);

            // If direct lookup failed (because clone's alpha-converted name differs from caller's),
            // resolve positionally through the pre-resolved monotypes. The i-th resolved type arg
            // corresponds to this clone's i-th type parameter.
            if (!bound_type && i < resolved_type_args.size && resolved_type_args.v[i]) {
                tl_monotype *src = resolved_type_args.v[i];
                if (tl_monotype_is_concrete(src)) {
                    bound_type = src;
                } else {
                    tl_monotype *resolved = tl_monotype_clone(self->arena, src);
                    tl_monotype_substitute(self->arena, resolved, self->subs, null);
                    if (tl_monotype_is_concrete(resolved)) bound_type = resolved;
                }
            }

#if DEBUG_EXPLICIT_TYPE_ARGS
            fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] type_param[%u]: name='%s', bound=%p\n", i,
                    str_cstr(&param_name), (void *)bound_type);
#endif

            if (bound_type) {

                tl_polytype *callsite_type = tl_polytype_absorb_mono(self->arena, bound_type);
#if DEBUG_EXPLICIT_TYPE_ARGS
                str type_str = tl_polytype_to_string(self->transient, callsite_type);
                fprintf(stderr, "[DEBUG CONCRETIZE TYPE PARAMS] setting type on '%s' to: %s\n",
                        str_cstr(&type_param->symbol.name), str_cstr(&type_str));
#endif
                ast_node_type_set(type_param, callsite_type);

                // Mirror the handling of value parameters: insert into env
                env_insert_constrain(self, param_name, callsite_type, type_param);
                tl_type_env_insert(self->env, param_name, callsite_type);
            }
        }
    }

    tl_monotype *inst_result = tl_monotype_sized_last(callsite->list.xs);
    body->type               = tl_polytype_absorb_mono(self->arena, inst_result);
}

// ============================================================================
// Free variable collection
// ============================================================================

int can_be_free_variable(tl_infer *self, traverse_ctx *traverse_ctx, ast_node const *node) {
    if (!ast_node_is_symbol(node) || traverse_ctx->is_field_name) return 0;

    str name = ast_node_str(node);

    // don't collect symbols which are nullary type literals
    if (tl_type_registry_is_nullary_type(self->registry, name)) return 0;

    // don't collect symbols that start with c_
    if (is_c_symbol(name)) return 0;

    // don't collect symbols that are already in lexical scope (e.g., union case bindings)
    if (str_hset_contains(traverse_ctx->lexical_names, name)) return 0;

    return 1;
}

int collect_free_variables_cb(tl_infer *self, traverse_ctx *traverse_ctx, ast_node *node) {
    if (ast_node_is_binary_op(node)) node = node->binary_op.left;
    if (!can_be_free_variable(self, traverse_ctx, node)) return 0;

    str name = ast_node_str(node);
    if (resolve_node(self, node, traverse_ctx, traverse_ctx->node_pos)) return 1;

    collect_free_variables_ctx *ctx      = traverse_ctx->user;

    tl_polytype                *type     = tl_type_env_lookup(self->env, name);
    int                         is_arrow = type && tl_monotype_is_arrow(type->type);

    // Note: arrow types in the environment are global functions and are not free variables. Note that
    // even local let-in-lambda functions are also in the environment, but their names will never clash
    // with function names.
    if (is_arrow || traverse_ctx_is_param(traverse_ctx, name)) {
        // FIXME: arrow type may exist because of a forward declaration, but function definition may be
        // missing. Need to report an error.
        ;
    } else {
        // a free variable
        dbg(self, "collect_free_variables_cb: add '%s'", str_cstr(&name));
        str_array_set_insert(&ctx->fvs, name);
    }

    // if symbol has a type which carries fvs, we also collect those.
    if (is_arrow && !tl_polytype_is_scheme(type)) {
        str_sized type_fvs = tl_monotype_fvs(type->type);
        forall(i, type_fvs) {
            if (!traverse_ctx_is_param(traverse_ctx, type_fvs.v[i]))
                str_array_set_insert(&ctx->fvs, type_fvs.v[i]);
        }
    }

    return 0;
}

void promote_free_variables(str_array *out, tl_monotype *in) {
    if (tl_monotype_is_list(in) || tl_monotype_is_tuple(in)) {
        // TODO: clean up tuple args handling
        forall(i, in->list.xs) promote_free_variables(out, in->list.xs.v[i]);
        forall(i, in->list.fvs) str_array_set_insert(out, in->list.fvs.v[i]);
    }
}

void add_free_variables_to_arrow(tl_infer *self, ast_node *node, tl_polytype *arrow) {
    // collect free variables from infer target and add to the generic's arrow type

    collect_free_variables_ctx ctx;
    ctx.fvs                    = (str_array){.alloc = self->arena};

    traverse_ctx *traverse_ctx = traverse_ctx_create(self->transient);
    traverse_ctx->user         = &ctx;
    int res                    = traverse_ast(self, traverse_ctx, node, collect_free_variables_cb);
    if (res) fatal("runtime error");

    array_shrink(ctx.fvs);
    dbg(self, "-- free variables: %u --", ctx.fvs.size);
    forall(i, ctx.fvs) {
        dbg(self, "%.*s", str_ilen(ctx.fvs.v[i]), str_buf(&ctx.fvs.v[i]));
    }

    // find any sublists with free variables and bring them to the top
    promote_free_variables(&ctx.fvs, arrow->type);

    // add free variables to arrow type
    if (ctx.fvs.size) {
        tl_monotype_absorb_fvs(arrow->type, (str_sized)sized_all(ctx.fvs));

        // sort free variables
        tl_monotype_sort_fvs(arrow->type);
    }
}
