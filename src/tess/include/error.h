#ifndef TESS_ERROR_H
#define TESS_ERROR_H

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TESS_ERROR_TAG_LIST(X)                                                                             \
    X(tess_err_ok, "ok")                                                                                   \
    /* tokenizer */                                                                                        \
    X(tess_err_eof, "eof")                                                                                 \
    X(tess_err_indent_too_long, "indent_too_long")                                                         \
    X(tess_err_invalid_token, "invalid_token")                                                             \
                                                                                                           \
    /* parser */                                                                                           \
    X(tess_err_unfinished_begin_end, "unfinished_begin_end")                                               \
    X(tess_err_unfinished_expression, "unfinished_expression")                                             \
    X(tess_err_unfinished_struct, "unfinished_struct")                                                     \
    X(tess_err_unfinished_lambda_declaration, "unfinished_lambda_declaration")                             \
    X(tess_err_expected_struct_name, "expected_struct_name")                                               \
    X(tess_err_expected_argument, "expected_argument")                                                     \
    X(tess_err_expected_expression, "expected_expression")                                                 \
    X(tess_err_expected_body, "expected_body")                                                             \
    X(tess_err_expected_keyword_then, "expected_keyword_then")                                             \
    X(tess_err_expected_keyword_else, "expected_keyword_else")                                             \
    X(tess_err_expected_value, "expected_value")                                                           \
    X(tess_err_expected_function_definition, "expected_function_definition")                               \
    X(tess_err_expected_if_condition, "expected_if_condition")                                             \
    X(tess_err_expected_if_then_arm, "expected_if_then_arm")                                               \
    X(tess_err_expected_if_else_arm, "expected_if_else_arm")                                               \
    X(tess_err_expected_lambda, "expected_lambda")                                                         \
    X(tess_err_expected_lambda_function_application_argument,                                              \
      "expected_lambda_function_application_argument")                                                     \
    X(tess_err_expected_function_application_argument, "expected_function_application_argument")           \
    X(tess_err_expected_specific_symbol, "expected_specific_symbol")                                       \
    X(tess_err_expected_string, "expected_string")                                                         \
    X(tess_err_expected_identifier, "expected_identifier")                                                 \
    X(tess_err_expected_type, "expected_type")                                                             \
    X(tess_err_expected_literal, "expected_literal")                                                       \
    X(tess_err_expected_number, "expected_number")                                                         \
    X(tess_err_expected_bool, "expected_bool")                                                             \
    X(tess_err_expected_operator, "expected_operator")                                                     \
    X(tess_err_expected_equal_sign, "expected_equal_sign")                                                 \
    X(tess_err_expected_arrow, "expected_arrow")                                                           \
    X(tess_err_expected_comma, "expected_comma")                                                           \
    X(tess_err_expected_dot, "expected_dot")                                                               \
    X(tess_err_expected_colon, "expected_colon")                                                           \
    X(tess_err_expected_colon_equal, "expected_colon_equal")                                               \
    X(tess_err_expected_end_of_block, "expected_end_of_block")                                             \
    X(tess_err_expected_end_of_expression, "expected_end_of_expression")                                   \
    X(tess_err_expected_open_round, "expected_open_round")                                                 \
    X(tess_err_expected_close_round, "expected_close_round")                                               \
    X(tess_err_expected_infix_operand, "expected_infix_operand")                                           \
    X(tess_err_expected_newline, "expected_newline")                                                       \
    X(tess_err_too_many_expressions, "too_many_expressions")                                               \
    X(tess_err_tokenizer_error, "tokenizer_error")                                                         \
    X(tess_err_type_exists, "type_exists")                                                                 \
                                                                                                           \
    /* analyzer */                                                                                         \
    X(tess_err_too_many_arguments, "too_many_arguments")

typedef enum tess_error_tag { TESS_ERROR_TAG_LIST(MOS_TAG_NAME) } tess_error_tag;

// -- utilities --

char const *tess_error_tag_to_string(tess_error_tag);

#endif
