#ifndef TESS_AST_TAGS_H
#define TESS_AST_TAGS_H

#include "ast.h"
#include "types.h"
#include "util.h"

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TL_AST_BIT_ARRAY     15
#define TL_AST_HAS_ARRAY(x)  TEST_BIT((x), TL_AST_BIT_ARRAY)
#define TL_AST_CLEAR_BITS(x) GET_FIELD((x), 0, TL_AST_BIT_ARRAY - 1)

typedef enum ast_tag : u16 {
    ast_nil,

    ast_address_of,
    ast_assignment,
    ast_bool,
    ast_dereference,
    ast_eof,
    ast_f64,
    ast_i64,
    ast_if_then_else,
    ast_infix,
    ast_let_in,
    ast_let_match_in,
    ast_string,
    ast_symbol,
    ast_u64,
    ast_user_type_definition,
    ast_user_type_get,
    ast_user_type_set,

    ast_begin_end = BIT(TL_AST_BIT_ARRAY),
    ast_function_declaration,
    ast_labelled_tuple,
    ast_lambda_declaration,
    ast_lambda_function,
    ast_lambda_function_application,
    ast_let,
    ast_named_function_application,
    ast_tuple,
    ast_user_type,

} ast_tag;

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
