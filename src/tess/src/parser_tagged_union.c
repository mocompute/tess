#include "parser_internal.h"

typedef struct {
    ast_node      *name;
    ast_node_array fields;
} tu_variant;

defarray(tu_variant_array, tu_variant);

static str tu_tag_name(allocator *arena, str tu_name) {
    return str_cat_3(arena, S("__"), tu_name, S("__Tag_"));
}

static str tu_union_name(allocator *arena, str tu_name) {
    return str_cat_3(arena, S("__"), tu_name, S("__Union_"));
}

static ast_node **clone_type_args(allocator *arena, u8 n, ast_node **src) {
    if (!n) return null;
    ast_node **dst = alloc_malloc(arena, n * sizeof(ast_node *));
    for (u8 i = 0; i < n; i++) dst[i] = ast_node_clone(arena, src[i]);
    return dst;
}

static void mangle_for_module_or_self(parser *self, ast_node *node, str module) {
    if (!str_is_empty(module)) mangle_name_for_module(self, node, module);
    else mangle_name(self, node);
}

// Auto-invoke a nullary tagged union variant accessed cross-module (e.g. Opt.Empty → value).
// Returns the wrapped AST node, or null if the symbol is not a nullary variant.
ast_node *maybe_auto_invoke_nullary_variant(parser *self, ast_node *symbol, str original_name,
                                            str target_module) {
    str *parent = str_map_get(self->nullary_variant_parents, symbol->symbol.name);
    if (!parent) return null;

    // Build scoped variant struct name: parent__variant (e.g., T__Empty)
    str       scoped  = str_qualify(self->ast_arena, *parent, original_name);
    ast_node *var_sym = ast_node_create_sym(self->ast_arena, scoped);
    mangle_name_for_module(self, var_sym, target_module);
    ast_node *inner_call =
      ast_node_create_nfa_tc(self->ast_arena, var_sym, (ast_node_sized){0}, (ast_node_sized){0});
    set_node_file(self, inner_call);
    return build_tagged_union_wrapping(self, *parent, original_name, target_module, inner_call);
}

// If parent_name is a tagged union, wrap the variant construction so it returns
// the tagged union type instead of the bare variant struct.
// For bare symbols (zero-field variants), promotes to a zero-arg NFA_TC first.
// Returns the wrapped node, or null if parent_name is not a tagged union.
ast_node *maybe_wrap_variant_in_tagged_union(parser *self, str parent_name, str child_name, str module,
                                             ast_node *right) {
    if (!str_hset_contains(self->tagged_union_variant_parents, parent_name)) return null;

    if (!ast_node_is_nfa(right)) {
        // Bare symbol case: Op.A (zero-field variant, no parentheses)
        // Promote to zero-arg NFA_TC then wrap (same pattern as None sugar)
        right = ast_node_create_nfa_tc(self->ast_arena, right, (ast_node_sized){0}, (ast_node_sized){0});
        set_node_file(self, right);
    }
    return build_tagged_union_wrapping(self, parent_name, child_name, module, right);
}

// Build the wrapping AST for a tagged union variant construction:
//   inner_call -> __Shape__Union_(Circle = inner_call)
//              -> __Shape__Tag_.Circle
//              -> Shape(tag = ..., u = ...)
// The 'module' parameter is used for cross-module mangling; pass str_empty() for same-module.
ast_node *build_tagged_union_wrapping(parser *self, str tu_name, str var_name, str module,
                                      ast_node *inner_call) {
    allocator *arena = self->ast_arena;

    // Union construction: __Shape__Union_(Circle = innerCall)
    ast_node *union_arg_name               = ast_node_create_sym(arena, var_name);
    ast_node *union_assign                 = ast_node_create_assignment(arena, union_arg_name, inner_call);
    union_assign->assignment.is_field_name = 1;
    set_node_file(self, union_assign);
    ast_node_array union_args = {.alloc = arena};
    array_push(union_args, union_assign);
    array_shrink(union_args);

    ast_node *union_call_name = ast_node_create_sym(arena, tu_union_name(arena, tu_name));
    mangle_for_module_or_self(self, union_call_name, module);
    ast_node *union_call = ast_node_create_nfa(arena, union_call_name, (ast_node_sized){0},
                                               (ast_node_sized)array_sized(union_args));
    set_node_file(self, union_call);

    // Tag access: __Shape__Tag_.Circle
    ast_node *tag_type = ast_node_create_sym(arena, tu_tag_name(arena, tu_name));
    mangle_for_module_or_self(self, tag_type, module);
    ast_node *dot_op      = ast_node_create_sym_c(arena, ".");
    ast_node *tag_variant = ast_node_create_sym(arena, var_name);
    ast_node *tag_access  = ast_node_create_binary_op(arena, dot_op, tag_type, tag_variant);
    set_node_file(self, tag_access);

    // Wrapper construction: Shape(tag = tagAccess, u = unionCall)
    ast_node *tag_arg_name               = ast_node_create_sym_c(arena, AST_TAGGED_UNION_TAG_FIELD);
    ast_node *tag_assign                 = ast_node_create_assignment(arena, tag_arg_name, tag_access);
    tag_assign->assignment.is_field_name = 1;
    set_node_file(self, tag_assign);

    ast_node *u_arg_name               = ast_node_create_sym_c(arena, AST_TAGGED_UNION_UNION_FIELD);
    ast_node *u_assign                 = ast_node_create_assignment(arena, u_arg_name, union_call);
    u_assign->assignment.is_field_name = 1;
    set_node_file(self, u_assign);

    ast_node_array wrapper_args = {.alloc = arena};
    array_push(wrapper_args, tag_assign);
    array_push(wrapper_args, u_assign);
    array_shrink(wrapper_args);

    ast_node *wrapper_call_name = ast_node_create_sym(arena, tu_name);
    mangle_for_module_or_self(self, wrapper_call_name, module);
    ast_node *wrapper_call = ast_node_create_nfa(arena, wrapper_call_name, (ast_node_sized){0},
                                                 (ast_node_sized)array_sized(wrapper_args));
    set_node_file(self, wrapper_call);

    return wrapper_call;
}

// Helper to create a constructor function for a tagged union variant
// E.g., Shape_Circle(radius: Float) -> Shape { ... }
// Note: Type parameters are inferred during type checking, not passed explicitly.
static ast_node *create_variant_constructor(parser *self,
                                            str     tu_name_str,  // e.g., "Shape"
                                            str     var_name_str, // e.g., "Circle"
                                            u8 n_type_args, // number of type params (unused, for future)
                                            ast_node **type_args, // type param nodes (unused, for future)
                                            ast_node_array var_fields) // variant fields
{
    allocator     *arena       = self->ast_arena;
    ast_node_sized type_params = {.size = n_type_args, .v = type_args};

    // 1. Create function name: unscoped at module level (e.g., "Circle")
    str       func_name_str = var_name_str;
    ast_node *func_name     = ast_node_create_sym(arena, func_name_str);

    // 2. Clone variant fields as function parameters
    ast_node_array params = {.alloc = arena};
    forall(i, var_fields) {
        ast_node *field = var_fields.v[i];
        ast_node *param = ast_node_create_sym(arena, field->symbol.name);
        if (field->symbol.annotation) {
            param->symbol.annotation = ast_node_clone(arena, field->symbol.annotation);
        }
        array_push(params, param);
    }
    array_shrink(params);

    // 3. Build the return type annotation
    // For non-generic: Shape
    // For generic: Shape[T]
    ast_node *return_type = null;
    if (n_type_args) {
        ast_node_sized args = {.size = n_type_args, .v = clone_type_args(arena, n_type_args, type_args)};
        ast_node      *wrapper_name = ast_node_create_sym(arena, tu_name_str);
        mangle_name(self, wrapper_name);
        // TYPE ANNOTATION NFA: Shape[T] — type params in type_args slot.
        return_type = ast_node_create_nfa(arena, wrapper_name, args, (ast_node_sized){0});
    } else {
        return_type = ast_node_create_sym(arena, tu_name_str);
        mangle_name(self, return_type);
    }

    // 4. Build the arrow annotation for function type: (params) -> ReturnType
    ast_node *arrow              = parser_make_arrow(self, params, return_type, type_params);
    func_name->symbol.annotation = arrow;

    // 5. Build the function body
    // Inner variant construction: Circle(radius = radius)
    ast_node_array inner_args = {.alloc = arena};
    forall(i, var_fields) {
        ast_node *field                      = var_fields.v[i];
        ast_node *arg_name                   = ast_node_create_sym(arena, field->symbol.name);
        ast_node *arg_val                    = ast_node_create_sym(arena, field->symbol.name);
        ast_node *arg_assign                 = ast_node_create_assignment(arena, arg_name, arg_val);
        arg_assign->assignment.is_field_name = 1; // Mark as field name to prevent renaming
        set_node_file(self, arg_assign);
        array_push(inner_args, arg_assign);
    }
    array_shrink(inner_args);

    // Inner call constructs the scoped variant struct (e.g., Shape__Circle)
    str       var_struct_str  = str_qualify(arena, tu_name_str, var_name_str);
    ast_node *inner_call_name = ast_node_create_sym(arena, var_struct_str);
    mangle_name(self, inner_call_name);
    // VALUE CONSTRUCTION NFA: Shape__Circle(radius = radius) — field assignments are value args.
    ast_node *inner_call = ast_node_create_nfa(arena, inner_call_name, (ast_node_sized){0},
                                               (ast_node_sized)array_sized(inner_args));
    set_node_file(self, inner_call);

    // Wrap inner_call in union + tag + wrapper struct using shared helper
    ast_node *wrapper_call =
      build_tagged_union_wrapping(self, tu_name_str, var_name_str, str_empty(), inner_call);

    // Create body with just the wrapper call
    ast_node_array body_exprs = {.alloc = arena};
    array_push(body_exprs, wrapper_call);
    array_shrink(body_exprs);
    ast_node *body = ast_node_create_body(arena, (ast_node_sized)array_sized(body_exprs));
    set_node_file(self, body);

    // Create the function (let) node
    add_module_symbol(self, func_name);
    mangle_name(self, func_name);
    ast_node *let =
      ast_node_create_let(arena, func_name, (ast_node_sized){0}, (ast_node_sized)array_sized(params), body);
    set_node_parameters(self, let, &params);
    let->let.name = func_name;
    let->let.body = body;
    set_node_file(self, let);

    return let;
}

// Tagged union syntax:
//   Shape = | Circle { radius: Float }
//           | Square { length: Float }
//           | Rectangle { length: Float, height: Float }
//
// Or with generics:
//   Option[T] : | Some { value: T }
//               | None
//
// Desugars to:
//   _ShapeTag_   : { Circle, Square, Rectangle }
//   Circle       : { radius: Float }
//   Square       : { length: Float }
//   Rectangle    : { length: Float, height: Float }
//   _ShapeUnion_ : { | Circle: Circle | Square: Square | Rectangle: Rectangle }
//   Shape        : { tag: _ShapeTag_, u: _ShapeUnion_ }
//
// Plus constructor functions:
//   Shape_Circle(radius: Float) -> Shape { ... }
//   Shape_Square(length: Float) -> Shape { ... }
//   Shape_Rectangle(length: Float, height: Float) -> Shape { ... }
//
int toplevel_tagged_union(parser *self) {
    // Parse type name (possibly with type parameters)
    if (a_try(self, a_type_identifier)) return 1;
    ast_node *type_ident = self->result;
    unmangle_name(self, type_ident);

    // Parse ':'
    if (a_try(self, a_colon)) return 1;

    // Parse first '|'
    if (a_try(self, a_vertical_bar)) return 1;

    // Extract type name and type arguments.
    // a_type_identifier -> a_funcall parses e.g. Option[T] with type params in .type_arguments.
    ast_node  *tu_name     = null;
    u8         n_type_args = 0;
    ast_node **type_args   = null;

    if (ast_node_is_symbol(type_ident)) {
        tu_name = type_ident;
    } else if (ast_node_is_nfa(type_ident)) {
        tu_name     = type_ident->named_application.name;
        n_type_args = type_ident->named_application.n_type_arguments;
        type_args   = type_ident->named_application.type_arguments;
    } else {
        return 1;
    }

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(tu_name)) return ERROR_STOP;

    str tu_name_str = tu_name->symbol.name;
    if (is_name_already_defined(self, tu_name_str)) {
        self->error.tag = tl_err_type_name_already_defined;
        return ERROR_STOP;
    }
    str_hset_insert(&self->nested_type_parents, tu_name_str);
    str_hset_insert(&self->tagged_union_variant_parents, tu_name_str);

    // Collect variants: { name, fields[] }
    tu_variant_array variants = {.alloc = self->ast_arena};

    while (1) {
        // Parse variant name
        if (a_try(self, a_identifier)) return ERROR_STOP;
        ast_node *var_name = self->result;

        // Existing-type variant syntax (Module.Type) is no longer supported
        if (0 == a_try(self, a_dot)) {
            self->error.tag = tl_err_expected_expression;
            return ERROR_STOP;
        }

        // Parse optional struct body { field: Type, ... }
        ast_node_array fields = {.alloc = self->ast_arena};
        if (0 == a_try(self, a_open_curly)) {
            while (1) {
                int saw_comma = 0;
                if (0 == a_try(self, a_comma)) saw_comma = 1;
                if (0 == a_try(self, a_close_curly)) break;
                if (!saw_comma && fields.size) {
                    if (a_try(self, a_comma)) return ERROR_STOP;
                }
                if (a_try(self, a_param)) return ERROR_STOP;
                array_push(fields, self->result);
            }
        }
        array_shrink(fields);

        // Check for reserved type keywords to disallow
        if (is_reserved_type_name(var_name)) return ERROR_STOP;
        if (is_name_already_defined(self, var_name->symbol.name)) {
            self->error.tag  = tl_err_type_name_already_defined;
            self->error.line = var_name->line;
            self->error.col  = var_name->col;
            self->error.file = var_name->file;
            return ERROR_STOP;
        }

        tu_variant v = {.name = var_name, .fields = fields};
        array_push(variants, v);

        // Check for next variant or end
        if (a_try(self, a_vertical_bar)) break; // no more variants
    }
    array_shrink(variants);

    if (!variants.size) return 1;

    // Generate all the desugared type definitions
    ast_node_array result_nodes = {.alloc = self->ast_arena};

    // 1. Tag enum: __Shape__Tag_ : { Circle, Square, Rectangle }
    {
        str            tag_name_str = tu_tag_name(self->ast_arena, tu_name_str);
        ast_node      *tag_name     = ast_node_create_sym(self->ast_arena, tag_name_str);

        ast_node_array tag_idents   = {.alloc = self->ast_arena};
        forall(i, variants) {
            ast_node *ident = ast_node_create_sym(self->ast_arena, variants.v[i].name->symbol.name);
            array_push(tag_idents, ident);
        }
        array_shrink(tag_idents);

        ast_node *tag_enum                        = create_enum_utd(self, tag_name, tag_idents);
        tag_enum->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, tag_name);
        mangle_name(self, tag_name);
        array_push(result_nodes, tag_enum);
    }

    // 2. Variant structs: Shape__Circle : { radius: Float }, etc.
    //    Scoped under tagged union type, accessed as Shape.Circle via nested_type_parents.
    forall(i, variants) {
        tu_variant *v            = &variants.v[i];

        str         var_name_str = str_qualify(self->ast_arena, tu_name_str, v->name->symbol.name);
        ast_node   *var_name     = ast_node_create_sym(self->ast_arena, var_name_str);

        // For generics, determine which type params are actually used by this variant's fields
        ast_node **var_type_args = null;
        u8         var_n_type_args =
          collect_used_type_params(self, n_type_args, type_args, v->fields, &var_type_args);

        ast_node *var_struct = create_utd(self, var_name, var_n_type_args, var_type_args, v->fields, 0);
        var_struct->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, var_name);
        mangle_name(self, var_name);
        array_push(result_nodes, var_struct);
    }

    // 3. Union type: __Shape__Union_ : { | Circle: Circle | Square: Square | ... }
    {
        str       union_name_str = tu_union_name(self->ast_arena, tu_name_str);
        ast_node *union_name     = ast_node_create_sym(self->ast_arena, union_name_str);

        // Build union fields: each field name is the variant name, annotation is the variant type
        ast_node_array union_fields = {.alloc = self->ast_arena};
        forall(i, variants) {
            tu_variant *v = &variants.v[i];

            // Field name (e.g., "Circle")
            ast_node *field_name = ast_node_create_sym(self->ast_arena, v->name->symbol.name);

            // Field annotation: the variant type (may be generic)
            ast_node **used_type_args = null;
            u8         n_used_type_args =
              collect_used_type_params(self, n_type_args, type_args, v->fields, &used_type_args);

            str       var_struct_name = str_qualify(self->ast_arena, tu_name_str, v->name->symbol.name);

            ast_node *field_ann       = null;
            if (n_used_type_args) {
                // Generic variant with used type params
                ast_node_sized args          = {.size = n_used_type_args, .v = used_type_args};
                ast_node      *var_type_name = ast_node_create_sym(self->ast_arena, var_struct_name);
                mangle_name(self, var_type_name);
                // TYPE ANNOTATION NFA: e.g. Shape__Circle[a] — type params in type_args slot.
                field_ann = ast_node_create_nfa(self->ast_arena, var_type_name, args, (ast_node_sized){0});
            } else {
                // Non-generic variant (no fields or no type params used)
                field_ann = ast_node_create_sym(self->ast_arena, var_struct_name);
                mangle_name(self, field_ann);
            }

            field_name->symbol.annotation = field_ann;
            array_push(union_fields, field_name);
        }
        array_shrink(union_fields);

        // Create the union type with all parent type params
        ast_node **union_type_args = clone_type_args(self->ast_arena, n_type_args, type_args);

        ast_node  *union_utd = create_utd(self, union_name, n_type_args, union_type_args, union_fields, 1);
        union_utd->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, union_name);
        mangle_name(self, union_name);
        array_push(result_nodes, union_utd);
    }

    // 4. Wrapper struct: Shape : { tag: _ShapeTag_, u: _ShapeUnion_ }
    {
        ast_node *wrapper_name = ast_node_create_sym(self->ast_arena, tu_name_str);

        // Build wrapper fields: tag and u
        ast_node_array wrapper_fields = {.alloc = self->ast_arena};

        // Field: tag: __Shape__Tag_
        {
            ast_node *tag_field    = ast_node_create_sym_c(self->ast_arena, AST_TAGGED_UNION_TAG_FIELD);
            str       tag_type_str = tu_tag_name(self->ast_arena, tu_name_str);
            ast_node *tag_ann      = ast_node_create_sym(self->ast_arena, tag_type_str);
            mangle_name(self, tag_ann);
            tag_field->symbol.annotation = tag_ann;
            array_push(wrapper_fields, tag_field);
        }

        // Field: u: __Shape__Union_ (or __Shape__Union_[T] for generics)
        {
            ast_node *u_field        = ast_node_create_sym_c(self->ast_arena, AST_TAGGED_UNION_UNION_FIELD);
            str       union_type_str = tu_union_name(self->ast_arena, tu_name_str);

            ast_node *u_ann          = null;
            if (n_type_args) {
                ast_node_sized args            = {.size = n_type_args,
                                                  .v    = clone_type_args(self->ast_arena, n_type_args, type_args)};
                ast_node      *union_type_name = ast_node_create_sym(self->ast_arena, union_type_str);
                mangle_name(self, union_type_name);
                // TYPE ANNOTATION NFA: __Shape__Union_[T] — type params in type_args slot.
                u_ann = ast_node_create_nfa(self->ast_arena, union_type_name, args, (ast_node_sized){0});
            } else {
                u_ann = ast_node_create_sym(self->ast_arena, union_type_str);
                mangle_name(self, u_ann);
            }

            u_field->symbol.annotation = u_ann;
            array_push(wrapper_fields, u_field);
        }
        array_shrink(wrapper_fields);

        // Create wrapper with all type params
        ast_node **wrapper_type_args = clone_type_args(self->ast_arena, n_type_args, type_args);

        ast_node  *wrapper_utd =
          create_utd(self, wrapper_name, n_type_args, wrapper_type_args, wrapper_fields, 0);
        wrapper_utd->user_type_def.tagged_union_name = tu_name_str;
        add_module_symbol(self, wrapper_name);
        mangle_name(self, wrapper_name);
        array_push(result_nodes, wrapper_utd);
    }

    // 5. Constructor functions for each variant
    forall(i, variants) {
        tu_variant *v    = &variants.v[i];

        ast_node   *ctor = create_variant_constructor(self, tu_name_str, v->name->symbol.name, n_type_args,
                                                      type_args, v->fields);
        array_push(result_nodes, ctor);

        // Record nullary variants for auto-invocation (bare B or cross-module Opt.Empty)
        if (v->fields.size == 0) {
            str key = !str_is_empty(self->current_module)
                        ? str_copy(self->parent_alloc,
                                   mangle_str_for_module(self, v->name->symbol.name, self->current_module))
                        : str_copy(self->parent_alloc, v->name->symbol.name);
            str_map_set(&self->nullary_variant_parents, key, &tu_name_str);
        }
    }

    array_shrink(result_nodes);

    // Return as a body containing all the generated UTDs and constructor functions
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)array_sized(result_nodes));
    set_node_file(self, body);
    return result_ast_node(self, body);
}
