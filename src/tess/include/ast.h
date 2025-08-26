#ifndef TESS_AST_H
#define TESS_AST_H

#include "ast_tags.h"
#include "mos_string.h"
#include "nodiscard.h"
#include "tess_type.h"

#include "vector.h"

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
            ast_operator     op;
            struct ast_node *left;
            struct ast_node *right;
        } infix;

        struct {
            vector           parameters;
            struct ast_node *body;
        } lambda_function;

        struct {
            struct ast_node *name;
            struct ast_node *value;
            struct ast_node *body;
        } let_in;

        struct {
            vector           parameters;
            struct ast_node *name;
        } function_declaration;

        struct {
            vector parameters;
        } lambda_declaration;

        struct {
            vector           parameters;
            struct ast_node *name;
            struct ast_node *body;
        } let;

        struct {
            struct ast_node *condition;
            struct ast_node *yes;
            struct ast_node *no;
        } if_then_else;

        struct {
            vector           arguments;
            struct ast_node *lambda;
        } lambda_application;

        struct {
            vector           arguments;
            struct ast_node *name;
            bool             specialized;
        } named_application;

        struct {
            vector elements;
        } tuple;
    };

    struct tess_type *type;
    ast_tag           tag;
} ast_node;

// -- ast_node --

ast_node   *ast_node_create(allocator *, ast_tag) mallocfun;
void        ast_node_init(allocator *, ast_node *, ast_tag);
void        ast_node_deinit(allocator *, ast_node *);
void        ast_node_replace(allocator *, ast_node *, ast_tag);
void        ast_node_move(ast_node *dst, ast_node *src);

char const *ast_node_name_string(ast_node const *);
int         ast_node_name_strcmp(ast_node const *, char const *);

// -- utilities --

char       *ast_node_to_string(allocator *alloc, ast_node const *node);
void        ast_vector_init(allocator *, vector *);

char const *ast_tag_to_string(ast_tag);
int         string_to_ast_operator(char const *, ast_operator *);

typedef void (*ast_op_fun)(void *, ast_node *);
typedef void (*ast_op_cfun)(void *, ast_node const *);

void ast_pool_dfs(void *, ast_node *, ast_op_fun);
void ast_pool_cdfs(void *, ast_node const *, ast_op_cfun);

#endif
