#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "mos_string.h"
#include "nodiscard.h"
#include "util.h" // needed for MOS_TAG_NAME
#include "vector.h"

#define TESS_TYPE_TAGS(X)                                                                                  \
    X(type_nil, "nil")                                                                                     \
    X(type_bool, "bool")                                                                                   \
    X(type_int, "int")                                                                                     \
    X(type_float, "float")                                                                                 \
    X(type_string, "string")                                                                               \
    X(type_tuple, "tuple")                                                                                 \
    X(type_arrow, "arrow")                                                                                 \
    X(type_type_var, "type_var")

typedef enum type_tag { TESS_TYPE_TAGS(MOS_TAG_NAME) } type_tag;

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

// TODO get rid of this, use an anon type in the tess_type union
struct arrow_type {
    size_t left;
    size_t right;
};

typedef struct tess_type {
    union {
        struct vector     tuple;
        struct arrow_type arrow;
        u32               val;
    };
    type_tag tag;
} tess_type;

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
    size_t val;
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

// -- ast_pool --

typedef struct ast_pool {
    struct vector data; // ast_node
} ast_pool;

typedef void (*ast_op_fun)(ast_pool *, ast_node *);
typedef void (*ast_op_cfun)(ast_pool const *, ast_node const *);

// -- allocation and deallocation --

void          tess_type_init(tess_type *, type_tag);
void          tess_type_init_type_var(tess_type *, u32);
nodiscard int tess_type_init_tuple(allocator *, tess_type *);
void          tess_type_init_arrow(tess_type *);
void          tess_type_deinit(allocator *, tess_type *);

ast_pool     *ast_pool_alloc(allocator *);
ast_pool     *ast_pool_create(allocator *) mallocfun;
void          ast_pool_dealloc(allocator *, ast_pool **);
void          ast_pool_destroy(allocator *, ast_pool **);
nodiscard int ast_pool_init(allocator *, ast_pool *);
void          ast_pool_deinit(allocator *, ast_pool *);

nodiscard int ast_node_init(allocator *, ast_node *, ast_tag);
void          ast_node_deinit(allocator *, ast_node *);
nodiscard int ast_node_replace(allocator *, ast_node *, ast_tag);

char const   *ast_node_name_string(ast_node const *);
int           ast_node_name_strcmp(ast_node const *, char const *);

// -- pool operations --
//
// [move_back] takes ownership of ast_node(s) and invalidates caller's copy

nodiscard int   ast_pool_move_back(allocator *, ast_pool *, ast_node *, ast_node_h *);
ast_node       *ast_pool_at(ast_pool *, ast_node_h);
ast_node const *ast_pool_cat(ast_pool const *, ast_node_h);

void            ast_pool_dfs(ast_pool *, ast_node_h, ast_op_fun);
void            ast_pool_cdfs(ast_pool const *, ast_node_h, ast_op_cfun);

// -- utilities --

char const   *type_tag_to_string(type_tag);
char const   *ast_tag_to_string(ast_tag);
int           string_to_ast_operator(char const *, ast_operator *);
nodiscard int ast_vector_init(allocator *, vector *);

nodiscard int ast_node_to_string_buf(ast_pool *, ast_node const *, char *, size_t);

#endif
