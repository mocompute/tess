#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "mos_string.h"
#include "nodiscard.h"

#include "vector.h"

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
    X(ast_infix, "infix")                                                                                  \
    X(ast_tuple, "tuple")                                                                                  \
    X(ast_let_in, "let_in")                                                                                \
    X(ast_let, "let")                                                                                      \
    X(ast_if_then_else, "if_then_else")                                                                    \
    X(ast_lambda_function, "lambda_function")                                                              \
    X(ast_function_declaration, "function_declaration")                                                    \
    X(ast_lambda_declaration, "lambda_declaration")                                                        \
    X(ast_lambda_function_application, "lambda_function_application")                                      \
    X(ast_named_function_application, "named_function_application")

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

typedef struct {
    u32 val;
} ast_node_h;

typedef struct ast_node {
    union {
        struct {
            string_t name;
            string_t original; // set by syntax_rename_variable
        } symbol;

        struct {
            bool val;
        } bool_;

        struct {
            i64 val;
        } i64;

        struct {
            u64 val;
        } u64;

        struct {
            f64 val;
        } f64;

        struct {
            ast_operator op;
            ast_node_h   left;
            ast_node_h   right;
        } infix;

        struct {
            vector     parameters;
            ast_node_h body;
        } lambda_function;

        struct {
            ast_node_h name;
            ast_node_h value;
            ast_node_h body;
        } let_in;

        struct {
            vector     parameters;
            ast_node_h name;
        } function_declaration;

        struct {
            vector parameters;
        } lambda_declaration;

        struct {
            vector     parameters;
            ast_node_h name;
            ast_node_h body;
        } let;

        struct {
            ast_node_h condition;
            ast_node_h yes;
            ast_node_h no;
        } if_then_else;

        struct {
            vector     arguments;
            ast_node_h lambda;
        } lambda_application;

        struct {
            vector     arguments;
            ast_node_h name;
            bool       specialized;
        } named_application;

        struct {
            vector elements;
        } tuple;
    };

    ast_tag tag;
} ast_node;

typedef struct ast_pool ast_pool;
typedef void (*ast_op_fun)(ast_pool *, ast_node *);
typedef void (*ast_op_cfun)(ast_pool const *, ast_node const *);

// -- allocation and deallocation --

ast_pool *ast_pool_create(allocator *) mallocfun;
void      ast_pool_destroy(ast_pool **);

// -- pool operations --
//
// [move_back] takes ownership of ast_node(s) and invalidates caller's copy

nodiscard int   ast_pool_move_back(ast_pool *, ast_node *, ast_node_h *);
ast_node       *ast_pool_at(ast_pool *, ast_node_h);
ast_node const *ast_pool_cat(ast_pool const *, ast_node_h);

void            ast_pool_dfs(ast_pool *, ast_node_h, ast_op_fun);
void            ast_pool_cdfs(ast_pool const *, ast_node_h, ast_op_cfun);

// -- ast_node --

nodiscard int ast_node_init(ast_pool *, ast_node *, ast_tag);
void          ast_node_deinit(ast_pool *, ast_node *);
nodiscard int ast_node_replace(ast_pool *, ast_node *, ast_tag);

char const   *ast_node_name_string(ast_node const *);
int           ast_node_name_strcmp(ast_node const *, char const *);

// -- utilities --

char const   *ast_tag_to_string(ast_tag);
int           string_to_ast_operator(char const *, ast_operator *);
nodiscard int ast_vector_init(ast_pool *, vector *);

nodiscard int ast_node_to_string_buf(ast_pool *, ast_node const *, char *, size_t);

#endif
