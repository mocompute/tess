#ifndef TESS_AST_TAGS_H
#define TESS_AST_TAGS_H

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TESS_AST_TAGS(X)                                                                                   \
    X(ast_eof, "eof")                                                                                      \
    X(ast_nil, "nil")                                                                                      \
    X(ast_bool, "bool")                                                                                    \
    X(ast_symbol, "symbol")                                                                                \
    X(ast_i64, "i64")                                                                                      \
    X(ast_u64, "u64")                                                                                      \
    X(ast_f64, "f64")                                                                                      \
    X(ast_string, "string")                                                                                \
    X(ast_user_type, "user_type")                                                                          \
    X(ast_infix, "infix")                                                                                  \
    X(ast_tuple, "tuple")                                                                                  \
    X(ast_let_in, "let_in")                                                                                \
    X(ast_let, "let")                                                                                      \
    X(ast_if_then_else, "if_then_else")                                                                    \
    X(ast_lambda_function, "lambda_function")                                                              \
    X(ast_function_declaration, "function_declaration")                                                    \
    X(ast_lambda_declaration, "lambda_declaration")                                                        \
    X(ast_lambda_function_application, "lambda_function_application")                                      \
    X(ast_named_function_application, "named_function_application")                                        \
    X(ast_user_type_definition, "user_type_definition")

typedef enum ast_tag { TESS_AST_TAGS(MOS_TAG_NAME) } ast_tag;

// -- ast_node --

#define TESS_AST_OPERATOR_TAGS(X)                                                                          \
    X(ast_op_addition, "+")                                                                                \
    X(ast_op_subtraction, "-")                                                                             \
    X(ast_op_multiplication, "*")                                                                          \
    X(ast_op_division, "/")                                                                                \
                                                                                                           \
    /* NB: see is_arithmetic and is_relational */                                                          \
                                                                                                           \
    X(ast_op_less_than, "<")                                                                               \
    X(ast_op_less_than_equal, "<=")                                                                        \
    X(ast_op_equal, "==")                                                                                  \
    X(ast_op_not_equal, "<>")                                                                              \
    X(ast_op_greater_than_equal, ">=")                                                                     \
    X(ast_op_greater_than, ">")                                                                            \
    X(ast_op_sentinel, null)

typedef enum ast_operator { TESS_AST_OPERATOR_TAGS(MOS_TAG_NAME) } ast_operator;

#endif
