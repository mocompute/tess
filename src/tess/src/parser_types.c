#include "parser_internal.h"

// Helper to check if an AST node references a type parameter name
int annotation_uses_type_param(ast_node *node, str param_name) {
    if (!node) return 0;

    if (ast_node_is_symbol(node)) {
        if (str_eq(node->symbol.name, param_name)) return 1;
        // Also check the annotation
        return annotation_uses_type_param(node->symbol.annotation, param_name);
    }

    if (ast_node_is_nfa(node)) {
        // Check the nfa arguments recursively.
        // Since it's possible type arguments are reference in both the explicit type arguments and the
        // value arguments, iterate through both.
        for (u32 i = 0; i < node->named_application.n_arguments; i++) {
            if (annotation_uses_type_param(node->named_application.arguments[i], param_name)) return 1;
        }
        for (u32 i = 0; i < node->named_application.n_type_arguments; i++) {
            if (annotation_uses_type_param(node->named_application.type_arguments[i], param_name)) return 1;
        }
    }

    if (ast_node_is_arrow(node)) {
        if (annotation_uses_type_param(node->arrow.left, param_name)) return 1;
        if (annotation_uses_type_param(node->arrow.right, param_name)) return 1;
        return 0;
    }

    if (ast_node_is_tuple(node)) {
        for (u8 i = 0; i < node->tuple.n_elements; i++) {
            if (annotation_uses_type_param(node->tuple.elements[i], param_name)) return 1;
        }
        return 0;
    }

    return 0;
}

// Nested struct name tracking for annotation rewriting
typedef struct {
    str        bare_name;     // e.g. "Bar"
    str        prefixed_name; // e.g. "Foo_Bar"
    u8         n_type_args;
    ast_node **type_args;
} nested_struct_info;

// Try to parse a nested struct header: identifier ':' '{'
// On success, sets self->result to the identifier node and returns 0.
// On failure, returns nonzero and a_try backtracks consumed tokens.
int a_nested_struct_header(parser *self) {
    if (a_try(self, a_identifier)) return 1;
    ast_node *ident = self->result;
    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;
    self->result = ident;
    return 0;
}

// Parse struct fields between { and }, handling nested struct definitions recursively.
// parent_prefix: name prefix for nested structs (e.g. "Foo")
// parent_n_type_args/parent_type_args: type params inherited from parent
// out_fields: populated with the parent's fields (nested struct entries removed)
// out_nested_utds: collects desugared nested struct UTD nodes
// Returns 0 on success, nonzero on failure.
int parse_struct_fields(parser *self, str parent_prefix, u8 parent_n_type_args, ast_node **parent_type_args,
                        ast_node_array *out_fields, ast_node_array *out_nested_utds) {

    // Track nested struct names for annotation rewriting
    struct {
        nested_struct_info *v;
        allocator          *alloc;
        u32                 size;
        u32                 cap;
    } nested_infos = {.v = null, .alloc = self->ast_arena};

    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1;
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && out_fields->size) {
            if (a_try(self, a_comma)) return 1;
        }

        // Try parsing a nested struct: identifier ':' '{'
        // a_try handles backtracking if the pattern doesn't match
        if (0 == a_try(self, a_nested_struct_header)) {
            ast_node *nested_ident = self->result;
            // a_nested_struct_header consumed identifier, ':', and '{'
            // Now parse the nested struct body (fields until '}')
            str nested_bare_name = nested_ident->symbol.name;
            str prefixed_name    = str_qualify(self->ast_arena, parent_prefix, nested_bare_name);

            // Recursively parse nested struct fields
            ast_node_array nested_fields = {.alloc = self->ast_arena};
            int res = parse_struct_fields(self, prefixed_name, parent_n_type_args, parent_type_args,
                                          &nested_fields, out_nested_utds);
            if (res) return res;
            array_shrink(nested_fields);

            // Determine which parent type params are used by this nested struct
            ast_node **used_type_args = null;
            u8 n_used_type_args       = collect_used_type_params(self, parent_n_type_args, parent_type_args,
                                                                 nested_fields, &used_type_args);

            // Create the nested struct UTD
            ast_node *nested_name = ast_node_create_sym(self->ast_arena, prefixed_name);
            ast_node *nested_utd =
              create_utd(self, nested_name, n_used_type_args, used_type_args, nested_fields, 0);
            add_module_symbol(self, nested_name);
            mangle_name(self, nested_name);
            str_hset_insert(&self->nested_type_parents, parent_prefix);
            array_push(*out_nested_utds, nested_utd);

            // Record info for annotation rewriting
            nested_struct_info info = {
              .bare_name     = nested_bare_name,
              .prefixed_name = prefixed_name,
              .n_type_args   = n_used_type_args,
              .type_args     = used_type_args,
            };
            array_push(nested_infos, info);

            continue; // don't add as a field
        }

        // Not a nested struct — parse as regular field
        if (a_try(self, a_param)) return saw_comma ? ERROR_STOP : 1;
        array_push(*out_fields, self->result);
    }
    array_shrink(*out_fields);

    // Rewrite field annotations: replace bare nested names with prefixed + parameterized versions
    if (nested_infos.size) {
        forall(fi, *out_fields) {
            ast_node *ann = out_fields->v[fi]->symbol.annotation;
            if (!ann) continue;

            for (u32 ni = 0; ni < nested_infos.size; ni++) {
                nested_struct_info *info = &nested_infos.v[ni];

                // Check if annotation is a bare symbol matching the nested name
                if (ast_node_is_symbol(ann) && str_eq(ann->symbol.name, info->bare_name)) {
                    if (info->n_type_args) {
                        // Replace with Foo_Bar(T1, ...)
                        ast_node_sized args = {
                          .size = info->n_type_args,
                          .v    = alloc_malloc(self->ast_arena, info->n_type_args * sizeof(ast_node *))};
                        for (u8 j = 0; j < info->n_type_args; j++) {
                            args.v[j] = ast_node_clone(self->ast_arena, info->type_args[j]);
                        }
                        ast_node *new_name = ast_node_create_sym(self->ast_arena, info->prefixed_name);
                        mangle_name(self, new_name);

                        ast_node *new_ann =
                          ast_node_create_nfa(self->ast_arena, new_name, args, (ast_node_sized){0});
                        out_fields->v[fi]->symbol.annotation = new_ann;
                    } else {
                        // Replace with just Foo_Bar (no type params)
                        ann->symbol.name = info->prefixed_name;
                        mangle_name(self, ann);
                    }
                    break;
                }
            }
        }
    }

    return 0;
}

// Shared post-field-parsing logic for struct and union type definitions.
// Creates the UTD node, checks for reserved names and unused type params (structs only),
// registers the module symbol, and wraps with nested UTDs if present (structs only).
int finalize_type_definition(parser *self, ast_node *type_ident, ast_node_array fields,
                             ast_node_array nested_utds, int is_union) {
    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    u8         n_type_args = 0;
    ast_node **type_args   = null;
    ast_node  *name        = null;

    if (ast_node_is_symbol(type_ident)) {
        name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        name        = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else fatal("logic error");

    ast_node *r = create_utd(self, name, n_type_args, type_args, fields, is_union);

    // Check for unused type parameters (structs only)
    if (!is_union && r->user_type_def.n_type_arguments) {
        for (u8 j = 0; j < r->user_type_def.n_type_arguments; j++) {
            int used = 0;
            for (u32 i = 0; i < r->user_type_def.n_fields; i++) {
                if (annotation_uses_type_param(r->user_type_def.field_annotations[i],
                                               r->user_type_def.type_arguments[j]->symbol.name)) {
                    used = 1;
                    break;
                }
            }
            if (!used) {
                self->error.tag = tl_err_unused_type_parameter;
                return ERROR_STOP;
            }
        }
    }

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);

    // If there are nested structs, return a body with nested UTDs first, then parent
    if (nested_utds.size) {
        ast_node_array result_nodes = {.alloc = self->ast_arena};
        forall(i, nested_utds) {
            array_push(result_nodes, nested_utds.v[i]);
        }
        array_push(result_nodes, r);
        array_shrink(result_nodes);

        ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)array_sized(result_nodes));
        set_node_file(self, body);
        return result_ast_node(self, body);
    }

    return result_ast_node(self, r);
}

int toplevel_struct(parser *self) {

    if (a_try(self, a_type_identifier)) return 1; // a_type_identifer mangles name
    ast_node *type_ident = self->result;

    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords before parsing fields
    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    // Extract parent name and type args for nested struct parsing
    ast_node  *parent_name = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        parent_name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        parent_name = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else fatal("logic error");

    ast_node_array fields      = {.alloc = self->ast_arena};
    ast_node_array nested_utds = {.alloc = self->ast_arena};
    int            res =
      parse_struct_fields(self, parent_name->symbol.name, n_type_args, type_args, &fields, &nested_utds);
    if (res) return res;

    return finalize_type_definition(self, type_ident, fields, nested_utds, 0);
}

int toplevel_union(parser *self) {

    if (a_try(self, a_type_identifier)) return 1; // mangles name
    ast_node *type_ident = self->result;

    if (a_try(self, a_colon)) return 1;

    // Format: MyUnion : { | variant1 : Type1 | variant2 : Type 2 }
    if (a_try(self, a_open_curly)) return 1;

    ast_node_array fields = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_vertical_bar)) return 1;
        if (a_try(self, a_param)) return ERROR_STOP;
        array_push(fields, self->result);
    }
    array_shrink(fields);

    ast_node_array no_nested_utds = {0};
    return finalize_type_definition(self, type_ident, fields, no_nested_utds, 1);
}

// Helper to collect type params used by a variant's fields
// Returns the number of used type params and fills used_type_args with the used params
u8 collect_used_type_params(parser *self, u8 n_type_args, ast_node **type_args, ast_node_array fields,
                            ast_node ***out_used_type_args) {
    if (!n_type_args || !fields.size) {
        *out_used_type_args = null;
        return 0;
    }

    // Track which type params are used
    u8 *used = alloc_malloc(self->ast_arena, n_type_args * sizeof(u8));
    for (u8 i = 0; i < n_type_args; i++) used[i] = 0;

    // Check each field's annotation for type param references
    forall(i, fields) {
        ast_node *ann = fields.v[i]->symbol.annotation;
        for (u8 j = 0; j < n_type_args; j++) {
            if (!used[j] && annotation_uses_type_param(ann, type_args[j]->symbol.name)) {
                used[j] = 1;
            }
        }
    }

    // Count and collect used type params
    u8 count = 0;
    for (u8 i = 0; i < n_type_args; i++) {
        if (used[i]) count++;
    }

    if (count == 0) {
        *out_used_type_args = null;
        return 0;
    }

    ast_node **result = alloc_malloc(self->ast_arena, count * sizeof(ast_node *));
    u8         idx    = 0;
    for (u8 i = 0; i < n_type_args; i++) {
        if (used[i]) {
            result[idx++] = ast_node_clone(self->ast_arena, type_args[i]);
        }
    }

    *out_used_type_args = result;
    return count;
}

// Helper to create a UTD node (struct or union) from name, type args, and fields
ast_node *create_utd(parser *self, ast_node *name, u8 n_type_args, ast_node **type_args,
                     ast_node_array fields, int is_union) {
    ast_node *r                       = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union         = is_union;
    r->user_type_def.name             = name;
    r->user_type_def.n_type_arguments = n_type_args;
    r->user_type_def.type_arguments   = type_args;
    r->user_type_def.n_fields         = fields.size;
    r->user_type_def.field_types      = null;

    if (fields.size) {
        r->user_type_def.field_names = alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
        r->user_type_def.field_annotations =
          alloc_malloc(self->ast_arena, fields.size * sizeof(ast_node *));
        forall(i, fields) {
            r->user_type_def.field_names[i]       = fields.v[i];
            r->user_type_def.field_annotations[i] = fields.v[i]->symbol.annotation;
        }
    } else {
        r->user_type_def.field_names       = null;
        r->user_type_def.field_annotations = null;
    }

    set_node_file(self, r);
    return r;
}

// Helper to create an enum UTD node
ast_node *create_enum_utd(parser *self, ast_node *name, ast_node_array idents) {
    ast_node *r                        = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union          = 0;
    r->user_type_def.name              = name;
    r->user_type_def.n_type_arguments  = 0;
    r->user_type_def.type_arguments    = null;
    r->user_type_def.field_types       = null;
    r->user_type_def.n_fields          = idents.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, idents.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = null;
    forall(i, idents) {
        r->user_type_def.field_names[i] = idents.v[i];
    }
    set_node_file(self, r);
    return r;
}

int toplevel_type_alias(parser *self) {
    ast_node *name = null;
    if (0 == a_try(self, a_attributed_identifier)) name = self->result;
    else return 1;
    if (a_try(self, a_equal_sign)) return 1;

    ast_node *target = null;
    if (0 == a_try(self, a_type_identifier)) target = self->result;
    else return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    add_module_symbol(self, name);
    mangle_name(self, name);
    ast_node *node = ast_node_create_type_alias(self->ast_arena, name, target);
    return result_ast_node(self, node);
}

int toplevel_enum(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon)) return 1;
    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node_array idents = {.alloc = self->ast_arena};
    while (1) {
        int saw_comma = 0;
        if (0 == a_try(self, a_comma)) saw_comma = 1; // optional comma
        if (0 == a_try(self, a_close_curly)) break;
        if (!saw_comma && idents.size) {
            // require comma separators
            if (a_try(self, a_comma)) return 1;
        }
        if (a_try(self, a_attributed_identifier))
            return 1; // enum must be an identifier; not mangled because access is through the type name
        array_push(idents, self->result);
    }
    array_shrink(idents);
    if (!idents.size) {
        array_free(idents);
        return 1;
    }

    add_module_symbol(self, name);
    mangle_name(self, name);

    // an enum uses the ast_user_type_definition with no type_arguments and no field_annotations. The actual
    // enums are saved in field_names.
    ast_node *r                       = ast_node_create(self->ast_arena, ast_user_type_definition);
    r->user_type_def.is_union         = 0;
    r->user_type_def.name             = name;
    r->user_type_def.n_type_arguments = 0;
    r->user_type_def.type_arguments   = null;
    r->user_type_def.field_types      = null;

    // The utd struct separates names from annotations, while they are both in the same symbol ast
    // node variant. So we have to do this splitting just for it to recombine later.
    r->user_type_def.n_fields          = idents.size;
    r->user_type_def.field_names       = alloc_malloc(self->ast_arena, idents.size * sizeof(ast_node *));
    r->user_type_def.field_annotations = null;
    forall(i, idents) {
        r->user_type_def.field_names[i] = idents.v[i];
    }

    return result_ast_node(self, r);
}

// Parse a comma-separated parameter list between ( and ).
// Returns 0 on success, error_code on mid-list failure, 1 if ( is missing.
// Shared by a_type_arrow and toplevel_defun.
int parse_param_list(parser *self, ast_node_array *out_params, int error_code) {
    if (a_try(self, a_open_round)) return 1;
    if (0 == a_try(self, a_close_round)) return 0;
    if (a_try(self, a_param)) return 1;
    array_push(*out_params, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) return 0;
        if (a_try(self, a_comma)) return error_code;
        if (a_try(self, a_param)) return error_code;
        array_push(*out_params, self->result);
    }
}

int maybe_type_argument_element(parser *self) {
    if (0 == a_try(self, a_type_identifier)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    return 1;
}

int maybe_type_arguments(parser *self, ast_node_array *type_args) {
    *type_args = (ast_node_array){.alloc = self->ast_arena};

    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) goto type_args_done;
        if (maybe_type_argument_element(self)) return ERROR_STOP;
        array_push(*type_args, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) goto type_args_done;
            if (a_try(self, a_comma)) return ERROR_STOP;
            if (maybe_type_argument_element(self)) return ERROR_STOP;
            array_push(*type_args, self->result);
        }
    }

type_args_done:
    return 0;
}

int a_type_constructor(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;
    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_field_assignment)) array_push(args, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_field_assignment)) return 1;
        array_push(args, self->result);
    }

done:
    array_shrink(args);
    mangle_name(self, name);

    maybe_mangle_implicit_submodule(self, name);

    ast_node *node = ast_node_create_nfa_tc(self->ast_arena, name, (ast_node_sized)sized_all(type_args),
                                            (ast_node_sized)sized_all(args));
    return result_ast_node(self, node);
}

int a_type_arrow(parser *self) {
    ast_node_array params = {.alloc = self->ast_arena};
    int            res    = parse_param_list(self, &params, ERROR_STOP);
    if (res) return res;

    if (a_try(self, a_arrow)) return 1;

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *rhs = self->result;

    // make tuple
    ast_node *tup = ast_node_create_tuple(self->ast_arena, (ast_node_sized)array_sized(params));
    set_node_file(self, tup);

    // make arrow: type arguments will be parsed separately
    ast_node *arrow = ast_node_create_arrow(self->ast_arena, tup, rhs, (ast_node_sized){0});
    set_node_file(self, arrow);

    return result_ast_node(self, arrow);
}
// Parse a single trait function signature: name(param: Type, ...) -> RetType
int a_trait_signature(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *sig_name = self->result;

    if (a_try(self, a_type_arrow)) return 1;
    ast_node *arrow             = self->result;

    sig_name->symbol.annotation = arrow;
    return result_ast_node(self, sig_name);
}

int toplevel_trait(parser *self) {
    // Parse trait declarations:
    //   Name[T] : { sig(a: T) -> T }
    //   Name[T] : Parent[T] { sig(a: T) -> T }
    //   Name[T] : Parent1[T], Parent2[T] { }

    if (a_try(self, a_type_identifier)) return 1;
    ast_node *type_ident = self->result;

    // Traits require type arguments — e.g. Eq[T]. Plain names are structs.
    if (!ast_node_is_nfa(type_ident)) return 1;
    if (type_ident->named_application.n_type_arguments == 0) return 1;

    if (a_try(self, a_colon)) return 1;

    if (is_reserved_type_name(type_ident)) return ERROR_STOP;

    // Determine parents vs body
    ast_node_array parents = {.alloc = self->ast_arena};

    // If next is not '{', parse parent trait list
    if (0 != a_try(self, a_open_curly)) {
        // Must be parent list: Name : Parent1[T], Parent2[T] { ... }
        if (a_try(self, a_type_identifier)) return 1;
        array_push(parents, self->result);

        while (0 == a_try(self, a_comma)) {
            if (a_try(self, a_type_identifier)) return ERROR_STOP;
            array_push(parents, self->result);
        }

        // Now expect '{'
        if (a_try(self, a_open_curly)) return ERROR_STOP;
    }

    // Parse signatures inside { ... }
    ast_node_array sigs = {.alloc = self->ast_arena};

    // Try to parse first signature to disambiguate from struct
    if (0 != a_try(self, a_close_curly)) {
        // Body is not empty — try to parse a signature
        if (a_try(self, a_trait_signature)) {
            // Not a trait signature — backtrack so struct parser can try
            if (!parents.size) return 1;
            // With parents but body doesn't parse — error
            return ERROR_STOP;
        }
        array_push(sigs, self->result);

        // Parse remaining signatures
        while (0 != a_try(self, a_close_curly)) {
            if (a_try(self, a_trait_signature)) return ERROR_STOP;
            array_push(sigs, self->result);
        }
    } else {
        // Empty body — only valid with parents (combined trait)
        if (!parents.size) return 1; // Empty body without parents → struct
    }

    // Extract name and type args
    ast_node  *name        = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        name        = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else return 1;

    // Create trait definition node
    ast_node *r                   = ast_node_create(self->ast_arena, ast_trait_definition);
    r->trait_def.name             = name;
    r->trait_def.n_type_arguments = n_type_args;
    r->trait_def.type_arguments   = type_args;

    array_shrink(sigs);
    r->trait_def.n_signatures = sigs.size;
    r->trait_def.signatures   = sigs.v;

    array_shrink(parents);
    r->trait_def.n_parents = parents.size;
    r->trait_def.parents   = parents.v;

    set_node_file(self, r);

    add_module_symbol(self, type_ident);
    mangle_name(self, type_ident);

    return result_ast_node(self, r);
}
