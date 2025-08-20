#ifndef TESS_ERROR_H
#define TESS_ERROR_H

#define TESS_ERROR_TAG_LIST(X)                                                                             \
  X(tess_err_ok, "ok")                                                                                     \
  /* tokenizer */                                                                                          \
  X(tess_err_eof, "eof")                                                                                   \
  X(tess_err_out_of_memory, "out_of_memory")                                                               \
  X(tess_err_indent_too_long, "indent_too_long")                                                           \
  X(tess_err_invalid_token, "invalid_token")                                                               \
                                                                                                           \
  /* parser */                                                                                             \
  X(tess_err_unfinished_let, "unfinished_let")                                                             \
  X(tess_err_unfinished_expression, "unfinished_expression")                                               \
  X(tess_err_unexpected_error, "unexpected_error")                                                         \
  X(tess_err_bad_indent, "bad_indent")                                                                     \
  X(tess_err_expected_argument, "expected_argument")                                                       \
  X(tess_err_expected_lambda, "expected_lambda")                                                           \
  X(tess_err_expected_symbol, "expected_symbol")                                                           \
  X(tess_err_expected_string, "expected_string")                                                           \
  X(tess_err_expected_identifier, "expected_identifier")                                                   \
  X(tess_err_expected_literal, "expected_literal")                                                         \
  X(tess_err_expected_number, "expected_number")                                                           \
  X(tess_err_expected_bool, "expected_bool")                                                               \
  X(tess_err_expected_operator, "expected_operator")                                                       \
  X(tess_err_expected_equal_sign, "expected_equal_sign")                                                   \
  X(tess_err_expected_arrow, "expected_arrow")                                                             \
  X(tess_err_expected_comma, "expected_comma")                                                             \
  X(tess_err_expected_open_round, "expected_open_round")                                                   \
  X(tess_err_expected_close_round, "expected_close_round")                                                 \
  X(tess_err_expected_infix_operand, "expected_infix_operand")                                             \
  X(tess_err_invalid_expression, "invalid_expression")                                                     \
  X(tess_err_reserved_symbol, "reserved_symbol")                                                           \
  X(tess_err_tokenizer_error, "tokenizer_error")                                                           \
                                                                                                           \
  /* analyzer */                                                                                           \
  X(tess_err_too_many_arguments, "too_many_arguments")                                                     \
  X(tess_err_symbol_exists, "symbol_exists")                                                               \
  X(tess_err_function_not_found, "function_not_found")                                                     \
  X(tess_err_type_error, "type_error")

#endif

#define TESS_ENUM(name, str) name,
typedef enum tess_error_tag { TESS_ERROR_TAG_LIST(TESS_ENUM) } tess_error_tag;
#undef TESS_ENUM

// -- utilities --

char const *tess_error_tag_to_string(tess_error_tag);
