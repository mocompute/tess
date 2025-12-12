#ifndef TESS_AST_TAGS_H
#define TESS_AST_TAGS_H

#include "types.h"
#include "util.h"

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TL_AST_BIT_ARRAY     15
#define TL_AST_HAS_ARRAY(x)  BIT_TEST((x), TL_AST_BIT_ARRAY)
#define TL_AST_CLEAR_BITS(x) FIELD_GET((x), 0, TL_AST_BIT_ARRAY - 1)

typedef enum ast_tag : u16 {
    ast_nil,
    ast_void,
    ast_hash_command,

    ast_arrow,
    ast_assignment,
    ast_binary_op,
    ast_body,
    ast_bool,
    ast_case,
    ast_char,
    ast_continue,
    ast_ellipsis,
    ast_eof,
    ast_f64,
    ast_i64,
    ast_if_then_else,
    ast_let_in,
    ast_reassignment,
    ast_reassignment_op,
    ast_return,
    ast_string,
    ast_symbol,
    ast_type_alias,
    ast_u64,
    ast_unary_op,
    ast_user_type_definition,
    ast_while,

    ast_lambda_function = BIT(TL_AST_BIT_ARRAY),
    ast_lambda_function_application,
    ast_let,
    ast_named_function_application,
    ast_tuple,

    // NOTE: update ast_tag_to_string

} ast_tag;

// -- ast_node --

#endif
