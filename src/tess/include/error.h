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
                                                                                                           \
    /* parser */                                                                                           \
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
    X(tl_err_expected_specific_symbol, "expected_specific_symbol")                                         \
    X(tl_err_expected_string, "expected_string")                                                           \
    X(tl_err_expected_addressable, "expected_addressable")                                                 \
    X(tl_err_expected_dereferenceable, "expected_dereferenceable")                                         \
    X(tl_err_expected_identifier, "expected_identifier")                                                   \
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
    X(tl_err_expected_colon, "expected_colon")                                                             \
    X(tl_err_expected_colon_equal, "expected_colon_equal")                                                 \
    X(tl_err_expected_end_of_block, "expected_end_of_block")                                               \
    X(tl_err_expected_end_of_expression, "expected_end_of_expression")                                     \
    X(tl_err_expected_open_round, "expected_open_round")                                                   \
    X(tl_err_expected_close_round, "expected_close_round")                                                 \
    X(tl_err_expected_newline, "expected_newline")                                                         \
    X(tl_err_expected_star, "expected_star")                                                               \
    X(tl_err_too_many_expressions, "too_many_expressions")                                                 \
    X(tl_err_tokenizer_error, "tokenizer_error")                                                           \
    X(tl_err_type_exists, "type_exists")                                                                   \
    X(tl_err_unexpected_inline_annotation, "unexpected_inline_annotation")                                 \
                                                                                                           \
    /* analyzer */                                                                                         \
    X(tl_err_too_many_arguments, "too_many_arguments")                                                     \
    X(tl_err_function_exists, "function_exists")                                                           \
    X(tl_err_not_compatible, "not_compatible")

typedef enum tl_error_tag { TESS_ERROR_TAG_LIST(MOS_TAG_NAME) } tl_error_tag;

// -- utilities --

char const *tl_error_tag_to_string(tl_error_tag);

#endif
