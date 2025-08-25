#ifndef TESS_AST_H
#define TESS_AST_H

#include "ast_pool.h"
#include "ast_tags.h"
#include "mos_string.h"
#include "nodiscard.h"

#include "vector.h"

struct ast_node {
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

    struct tess_type *type;
    ast_tag           tag;
};

// -- ast_node --

nodiscard int ast_node_init(ast_pool *, struct ast_node *, ast_tag);
void          ast_node_deinit(ast_pool *, struct ast_node *);
nodiscard int ast_node_replace(ast_pool *, struct ast_node *, ast_tag);

char const   *ast_node_name_string(struct ast_node const *);
int           ast_node_name_strcmp(struct ast_node const *, char const *);

// -- utilities --

nodiscard int ast_vector_init(ast_pool *, vector *);

char const   *ast_tag_to_string(ast_tag);
nodiscard int ast_node_to_string_buf(ast_pool *, struct ast_node const *, char *, size_t);
int           string_to_ast_operator(char const *, ast_operator *);

#endif
