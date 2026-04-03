#ifndef TESS_ERROR_H
#define TESS_ERROR_H

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TESS_ERROR_TAG_LIST(X)                                                                             \
    X(tl_err_ok, "ok")                                                                                     \
    /* tokenizer */                                                                                        \
    X(tl_err_eof, "eof")                                                                                   \
    X(tl_err_indent_too_long, "indent_too_long")                                                           \
    X(tl_err_invalid_token, "invalid_token")                                                               \
    X(tl_err_unexpected_endif, "unexpected_endif")                                                         \
                                                                                                           \
    /* parser */                                                                                           \
    X(tl_err_reserved_keyword, "reserved_keyword")                                                         \
    X(tl_err_expected_toplevel, "expected_toplevel")                                                       \
    X(tl_err_expected_funcall, "expected_funcall")                                                         \
    X(tl_err_expected_hash_command, "expected_hash_command")                                               \
    X(tl_err_expected_module, "expected_module")                                                           \
    X(tl_err_unfinished_begin_end, "unfinished_begin_end")                                                 \
    X(tl_err_unfinished_expression, "unfinished_expression")                                               \
    X(tl_err_unfinished_struct, "unfinished_struct")                                                       \
    X(tl_err_unfinished_lambda_declaration, "unfinished_lambda_declaration")                               \
    X(tl_err_expected_struct_name, "expected_struct_name")                                                 \
    X(tl_err_expected_argument, "expected_argument")                                                       \
    X(tl_err_expected_assignment_value, "expected_assignment_value")                                       \
    X(tl_err_expected_body, "expected_body")                                                               \
    X(tl_err_expected_declaration, "expected_declaration")                                                 \
    X(tl_err_expected_dereference_assign_value, "expected_dereference_assign_value")                       \
    X(tl_err_expected_expression, "expected_expression")                                                   \
    X(tl_err_expected_keyword_then, "expected_keyword_then")                                               \
    X(tl_err_expected_keyword_else, "expected_keyword_else")                                               \
    X(tl_err_expected_value, "expected_value")                                                             \
    X(tl_err_expected_function_definition, "expected_function_definition")                                 \
    X(tl_err_expected_if_condition, "expected_if_condition")                                               \
    X(tl_err_expected_if_then_arm, "expected_if_then_arm")                                                 \
    X(tl_err_expected_if_else_arm, "expected_if_else_arm")                                                 \
    X(tl_err_expected_lambda, "expected_lambda")                                                           \
    X(tl_err_expected_lambda_function_application_argument,                                                \
      "expected_lambda_function_application_argument")                                                     \
    X(tl_err_expected_let_in_value, "expected_let_in_value")                                               \
    X(tl_err_expected_function_application_argument, "expected_function_application_argument")             \
    X(tl_err_expected_nil, "expected_nil")                                                                 \
    X(tl_err_expected_semicolon, "expected_semicolon")                                                     \
    X(tl_err_expected_symbol, "expected_symbol")                                                           \
    X(tl_err_expected_specific_symbol, "expected_specific_symbol")                                         \
    X(tl_err_expected_string, "expected_string")                                                           \
    X(tl_err_expected_addressable, "expected_addressable")                                                 \
    X(tl_err_expected_dereferenceable, "expected_dereferenceable")                                         \
    X(tl_err_expected_identifier, "expected_identifier")                                                   \
    X(tl_err_expected_identifier_or_nil, "expected_identifier_or_nil")                                     \
    X(tl_err_expected_type, "expected_type")                                                               \
    X(tl_err_expected_literal, "expected_literal")                                                         \
    X(tl_err_expected_number, "expected_number")                                                           \
    X(tl_err_expected_bool, "expected_bool")                                                               \
    X(tl_err_expected_operator, "expected_operator")                                                       \
    X(tl_err_expected_equal_sign, "expected_equal_sign")                                                   \
    X(tl_err_expected_ampersand, "expected_ampersand")                                                     \
    X(tl_err_expected_arrow, "expected_arrow")                                                             \
    X(tl_err_expected_comma, "expected_comma")                                                             \
    X(tl_err_expected_dot, "expected_dot")                                                                 \
    X(tl_err_expected_ellipsis, "expected_ellipsis")                                                       \
    X(tl_err_expected_colon, "expected_colon")                                                             \
    X(tl_err_expected_colon_equal, "expected_colon_equal")                                                 \
    X(tl_err_expected_double_colon, "expected_double_colon")                                               \
    X(tl_err_expected_double_open_square, "expected_double_open_square")                                   \
    X(tl_err_expected_double_close_square, "expected_double_close_square")                                 \
    X(tl_err_expected_end_of_block, "expected_end_of_block")                                               \
    X(tl_err_expected_end_of_expression, "expected_end_of_expression")                                     \
    X(tl_err_expected_open_round, "expected_open_round")                                                   \
    X(tl_err_expected_close_round, "expected_close_round")                                                 \
    X(tl_err_expected_open_curly, "expected_open_curly")                                                   \
    X(tl_err_expected_close_curly, "expected_close_curly")                                                 \
    X(tl_err_expected_open_square, "expected_open_square")                                                 \
    X(tl_err_expected_close_square, "expected_close_square")                                               \
    X(tl_err_expected_newline, "expected_newline")                                                         \
    X(tl_err_expected_vertical_bar, "expected_vertical_bar")                                               \
    X(tl_err_expected_star, "expected_star")                                                               \
    X(tl_err_too_many_expressions, "too_many_expressions")                                                 \
    X(tl_err_tokenizer_error, "tokenizer_error")                                                           \
    X(tl_err_type_exists, "type_exists")                                                                   \
    X(tl_err_trait_circular_inheritance, "trait_circular_inheritance")                                     \
    X(tl_err_attributes_exist, "attributes_exist")                                                         \
    X(tl_err_unexpected_inline_annotation, "unexpected_inline_annotation")                                 \
    X(tl_err_unexpected_else, "unexpected_else")                                                           \
    X(tl_err_double_underscore_in_identifier, "double_underscore_in_identifier")                           \
    X(tl_err_alias_source_not_found, "alias_source_not_found")                                             \
    X(tl_err_alias_conflicts_with_module, "alias_conflicts_with_module")                                   \
    X(tl_err_alias_already_defined, "alias_already_defined")                                               \
    X(tl_err_alias_self_alias, "alias_self_alias")                                                         \
    X(tl_err_alias_invalid_name, "alias_invalid_name")                                                     \
    X(tl_err_alias_source_is_alias, "alias_source_is_alias")                                               \
    X(tl_err_alias_source_is_main, "alias_source_is_main")                                                 \
    X(tl_err_unalias_not_found, "unalias_not_found")                                                       \
                                                                                                           \
    /* analyzer */                                                                                         \
    X(tl_err_expected_pointer, "expected_pointer")                                                         \
    X(tl_err_expected_integer, "expected_integer")                                                         \
    X(tl_err_expected_type_constructor, "expected_type_constructor")                                       \
    X(tl_err_expected_type_alias_symbol, "expected_type_alias_symbol")                                     \
    X(tl_err_unresolved_type, "unresolved_type")                                                           \
    X(tl_err_arity, "wrong_number_of_arguments")                                                           \
    X(tl_err_unknown_hash_command, "unknown_hash_command")                                                 \
    X(tl_err_unknown_type, "unknown_type")                                                                 \
    X(tl_err_too_many_arguments, "too_many_arguments")                                                     \
    X(tl_err_function_exists, "function_exists")                                                           \
    X(tl_err_not_compatible, "not_compatible")                                                             \
    X(tl_err_no_main_function, "no_main_function")                                                         \
    X(tl_err_main_function_bad_type, "main_function_bad_type")                                             \
    X(tl_err_invalid_toplevel, "invalid_toplevel_node_type")                                               \
    X(tl_err_function_not_found, "function_not_found")                                                     \
    X(tl_err_free_variable_not_found, "free_variable_not_found")                                           \
    X(tl_err_type_error, "type_error")                                                                     \
    X(tl_err_field_not_found, "field_not_found")                                                           \
    X(tl_err_polymorphic_function_argument, "polymorphic_function_argument")                               \
    X(tl_err_unknown_symbol_in_main, "unknown symbol in main()")                                           \
    X(tl_err_unexpected_type_literal, "unexpected_type_literal")                                           \
    X(tl_err_tagged_union_missing_case, "tagged_union_missing_case")                                       \
    X(tl_err_tagged_union_unknown_variant, "tagged_union_unknown_variant")                                 \
    X(tl_err_tagged_union_expected_tagged_union, "tagged_union_expected_tagged_union")                     \
    X(tl_err_tagged_union_case_syntax_error, "tagged_union_case_syntax_error")                             \
    X(tl_err_unused_type_parameter, "unused_type_parameter")                                               \
    X(tl_err_nested_module_parent_not_found, "nested_module_parent_not_found")                             \
    X(tl_err_const_violation, "const_violation")                                                           \
    X(tl_err_try_requires_two_variant_union, "try_requires_two_variant_union")                             \
    X(tl_err_try_requires_single_field_variant, "try_requires_single_field_variant")                       \
    X(tl_err_else_binding_requires_two_variant_union, "else_binding_requires_two_variant_union")           \
    X(tl_err_trait_bound_not_satisfied, "trait_bound_not_satisfied")                                       \
    X(tl_err_closure_escape, "closure_escape")                                                             \
    X(tl_err_capture_without_alloc, "capture_without_alloc")                                               \
    X(tl_err_alloc_missing_capture, "alloc_missing_capture")                                               \
    X(tl_err_capture_unlisted_var, "capture_unlisted_variable")                                            \
    X(tl_err_capture_unused_var, "capture_unused_variable")                                                \
    X(tl_err_capture_not_in_scope, "capture_not_in_scope")                                                 \
    X(tl_err_alloc_expr_type_mismatch, "alloc_expr_type_mismatch")                                         \
    X(tl_err_undeclared_reassignment, "undeclared_variable")                                               \
    X(tl_err_auto_collapse_ambiguous, "auto_collapse_ambiguous")                                           \
    X(tl_err_invalid_format_spec, "invalid_format_specifier")

typedef enum tl_error_tag { TESS_ERROR_TAG_LIST(MOS_TAG_NAME) } tl_error_tag;

// -- utilities --

char const *tl_error_tag_to_string(tl_error_tag);
char const *tl_error_tag_to_user_string(tl_error_tag);

#endif
