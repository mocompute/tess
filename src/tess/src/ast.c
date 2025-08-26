#include "ast.h"
#include "alloc.h"
#include "dbg.h"
#include "mos_string.h"
#include "sexp.h"
#include "tess_type.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

// -- ast_node init and deinit --

void ast_node_deinit(allocator *alloc, struct ast_node *node) {

#define deinit(P) vec_deinit(alloc, &P)

    switch (node->tag) {
    case ast_lambda_function:             deinit(node->lambda_function.parameters); break;
    case ast_function_declaration:        deinit(node->function_declaration.parameters); break;
    case ast_lambda_declaration:          deinit(node->lambda_declaration.parameters); break;
    case ast_let:                         deinit(node->let.parameters); break;
    case ast_tuple:                       deinit(node->tuple.elements); break;
    case ast_lambda_function_application: deinit(node->lambda_application.arguments); break;
    case ast_named_function_application:  deinit(node->named_application.arguments); break;
    case ast_symbol:                      mos_string_deinit(alloc, &node->symbol.name); break;
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_let_in:
    case ast_if_then_else:                break;
    }

#undef deinit
}

ast_node *ast_node_create(allocator *alloc, ast_tag tag) {
    ast_node *self = alloc_calloc(alloc, 1, sizeof *self);
    self->tag      = tag;
    return self;
}

void ast_node_init(allocator *alloc, ast_node *node, ast_tag tag) {

    // accepts pool = null in some cases

#define init(P)                                                                                            \
    do {                                                                                                   \
        return ast_vector_init(alloc, &P);                                                                 \
    } while (0)

    alloc_zero(node);
    node->tag = tag;

    switch (node->tag) {
    case ast_lambda_function:             init(node->lambda_function.parameters);
    case ast_function_declaration:        init(node->function_declaration.parameters);
    case ast_lambda_declaration:          init(node->lambda_declaration.parameters);
    case ast_let:                         init(node->let.parameters);
    case ast_tuple:                       init(node->tuple.elements);
    case ast_lambda_function_application: init(node->lambda_application.arguments);
    case ast_named_function_application:  init(node->named_application.arguments);

    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_let_in:
    case ast_if_then_else:                return;
    }

#undef init
}

void ast_node_replace(allocator *alloc, ast_node *node, ast_tag tag) {
    ast_node_deinit(alloc, node);
    return ast_node_init(alloc, node, tag);
}

void ast_node_move(ast_node *dst, ast_node *src) {
    alloc_copy(dst, src);
    alloc_zero(src); // valid nil node
}

char const *ast_node_name_string(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag) return null;

    return mos_string_str(&node->symbol.name);
}

int ast_node_name_strcmp(ast_node const *node, char const *target) {
    char const *name = ast_node_name_string(node);
    if (!name) return false;
    return strcmp(name, target);
}

char const *ast_operator_to_string(ast_operator);

//

static void map_ast_node_to_sexp(void *, void *, void const *); // vec_map_fun

sexp        ast_node_to_sexp(allocator *alloc, ast_node const *node) {

#define pair(...)   sexp_init_list_pair(__VA_ARGS__)
#define triple(...) sexp_init_list_triple(__VA_ARGS__)
#define quad(...)   sexp_init_list_quad(__VA_ARGS__)
#define penta(...)  sexp_init_list_penta(__VA_ARGS__)

    if (null == node) return sexp_init_boxed(alloc);

    sexp type;
    {
        char *str = tess_type_to_string(alloc, node->type);
        type      = sexp_init_sym(alloc, str);
        alloc_free(alloc, str);
    }

    switch (node->tag) {

    case ast_eof: return pair(alloc, sexp_init_sym(alloc, "eof"), type);
    case ast_nil: return pair(alloc, sexp_init_sym(alloc, "nil"), type);
    case ast_bool:
        return pair(alloc, node->bool_.val ? sexp_init_sym(alloc, "true") : sexp_init_sym(alloc, "false"),
                    type);

    case ast_symbol:
        return triple(alloc, sexp_init_sym(alloc, "symbol"),
                      sexp_init_sym(alloc, mos_string_str(&node->symbol.name)), type);

    case ast_i64:
        return triple(alloc, sexp_init_sym(alloc, "i64"), sexp_init_i64(alloc, node->i64.val), type);
    case ast_u64:
        return triple(alloc, sexp_init_sym(alloc, "u64"), sexp_init_u64(alloc, node->u64.val), type);
    case ast_f64:
        return triple(alloc, sexp_init_sym(alloc, "f64"), sexp_init_f64(alloc, node->f64.val), type);

    case ast_string:
        return triple(alloc, sexp_init_sym(alloc, "string"),
                      sexp_init_sym(alloc, mos_string_str(&node->symbol.name)), type);

    case ast_infix:
        return penta(alloc, sexp_init_sym(alloc, "infix"),
                     sexp_init_sym(alloc, ast_operator_to_string(node->infix.op)),
                     ast_node_to_sexp(alloc, node->infix.left), ast_node_to_sexp(alloc, node->infix.right),
                     type);

    case ast_tuple: {

        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->tuple.elements));
        vec_map(&node->tuple.elements, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->tuple.elements));
        alloc_free(alloc, elements);
        return triple(alloc, sexp_init_sym(alloc, "tuple"), list, type);

    } break;

    case ast_let_in:
        return penta(alloc, sexp_init_sym(alloc, "let-in"), ast_node_to_sexp(alloc, node->let_in.name),
                     ast_node_to_sexp(alloc, node->let_in.value),
                     ast_node_to_sexp(alloc, node->let_in.body), type);

    case ast_let: {
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->let.parameters));
        vec_map(&node->let.parameters, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->let.parameters));
        alloc_free(alloc, elements);

        return penta(alloc, sexp_init_sym(alloc, "let"), ast_node_to_sexp(alloc, node->let.name), list,
                     ast_node_to_sexp(alloc, node->let_in.body), type);

    } break;

    case ast_if_then_else:
        return penta(alloc, sexp_init_sym(alloc, "if-then-else"),
                     ast_node_to_sexp(alloc, node->if_then_else.condition),
                     ast_node_to_sexp(alloc, node->if_then_else.yes),
                     ast_node_to_sexp(alloc, node->if_then_else.no), type);

    case ast_lambda_function: {
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->lambda_function.parameters));
        vec_map(&node->lambda_function.parameters, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->lambda_function.parameters));
        alloc_free(alloc, elements);

        return quad(alloc, sexp_init_sym(alloc, "lambda"), list,
                    ast_node_to_sexp(alloc, node->lambda_function.body), type);

    } break;

    case ast_function_declaration: {

        sexp *elements =
          alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->function_declaration.parameters));
        vec_map(&node->function_declaration.parameters, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->function_declaration.parameters));
        alloc_free(alloc, elements);

        return quad(alloc, sexp_init_sym(alloc, "function-declaration"), list,
                    ast_node_to_sexp(alloc, node->lambda_function.body), type);

    } break;

    case ast_lambda_declaration: {
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->lambda_declaration.parameters));
        vec_map(&node->lambda_declaration.parameters, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->lambda_declaration.parameters));
        alloc_free(alloc, elements);

        return triple(alloc, sexp_init_sym(alloc, "lambda-declaration"), list, type);

    } break;

    case ast_lambda_function_application: {
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->lambda_application.arguments));
        vec_map(&node->lambda_application.arguments, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->lambda_application.arguments));
        alloc_free(alloc, elements);

        return quad(alloc, sexp_init_sym(alloc, "lambda-application"),
                    ast_node_to_sexp(alloc, node->lambda_application.lambda), list, type);

    } break;
    case ast_named_function_application: {
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * vec_size(&node->named_application.arguments));
        vec_map(&node->named_application.arguments, map_ast_node_to_sexp, alloc, elements);
        sexp list = sexp_init_list(alloc, elements, vec_size(&node->named_application.arguments));
        alloc_free(alloc, elements);

        return quad(alloc, sexp_init_sym(alloc, "named-application"),
                    ast_node_to_sexp(alloc, node->named_application.name), list, type);

    } break;
    }
}

void map_ast_node_to_sexp(void *alloc, void *out, void const *node_ptr) {
    *(sexp *)out = ast_node_to_sexp(alloc, *(ast_node const **)node_ptr);
}

sexp ast_node_to_sexp_with_type(allocator *alloc, ast_node const *node) {

    sexp  expr = ast_node_to_sexp(alloc, node);

    char *type = tess_type_to_string(alloc, node->type);

    sexp  list = sexp_init_list_pair(alloc, expr, sexp_init_sym(alloc, type));

    alloc_free(alloc, type);
    return list;
}

// -- pool operations --

void ast_pool_dfs(void *ctx, ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    switch (node->tag) {
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string: return fun(ctx, node);

    case ast_infix:
        ast_pool_dfs(ctx, node->infix.left, fun);
        ast_pool_dfs(ctx, node->infix.right, fun);
        return fun(ctx, node);

    case ast_tuple: {
        ast_node **it  = (ast_node **)vec_begin(&node->tuple.elements);
        ast_node **end = (ast_node **)vec_end(&node->tuple.elements);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        return fun(ctx, node);
    } break;

    case ast_let_in:
        ast_pool_dfs(ctx, node->let_in.name, fun);
        ast_pool_dfs(ctx, node->let_in.value, fun);
        ast_pool_dfs(ctx, node->let_in.body, fun);

        return fun(ctx, node);

    case ast_let: {
        ast_pool_dfs(ctx, node->let.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->let.parameters);
        ast_node **end = (ast_node **)vec_end(&node->let.parameters);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        ast_pool_dfs(ctx, node->let.body, fun);

        return fun(ctx, node);
    } break;

    case ast_if_then_else:
        ast_pool_dfs(ctx, node->if_then_else.condition, fun);
        ast_pool_dfs(ctx, node->if_then_else.yes, fun);
        ast_pool_dfs(ctx, node->if_then_else.no, fun);

        return fun(ctx, node);

    case ast_lambda_function: {
        ast_node **it  = (ast_node **)vec_begin(&node->lambda_function.parameters);
        ast_node **end = (ast_node **)vec_end(&node->lambda_function.parameters);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        ast_pool_dfs(ctx, node->lambda_function.body, fun);

        return fun(ctx, node);
    } break;

    case ast_function_declaration: {
        ast_pool_dfs(ctx, node->function_declaration.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->function_declaration.parameters);
        ast_node **end = (ast_node **)vec_end(&node->function_declaration.parameters);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        return fun(ctx, node);
    } break;

    case ast_lambda_declaration: {
        ast_node **it  = (ast_node **)vec_begin(&node->lambda_declaration.parameters);
        ast_node **end = (ast_node **)vec_end(&node->lambda_declaration.parameters);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        return fun(ctx, node);

    } break;

    case ast_lambda_function_application: {
        ast_pool_dfs(ctx, node->lambda_application.lambda, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->lambda_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->lambda_application.arguments);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        return fun(ctx, node);
    } break;

    case ast_named_function_application: {
        ast_pool_dfs(ctx, node->named_application.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->named_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->named_application.arguments);
        while (it != end) ast_pool_dfs(ctx, *it++, fun);

        return fun(ctx, node);
    } break;
    }
}

void ast_pool_cdfs(void *ctx, ast_node const *start, ast_op_cfun fun) {
    ast_pool_dfs(ctx, (ast_node *)start, (ast_op_fun)fun);
}

// -- utilities --

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *ast_tag_to_string(ast_tag tag) {

    static char const *const strings[] = {TESS_AST_TAGS(MOS_TAG_STRING)};

    return strings[tag];
}

char const *ast_operator_to_string(ast_operator tag) {

    static char const *const strings[] = {TESS_AST_OPERATOR_TAGS(MOS_TAG_STRING)};

    return strings[tag];
}

int string_to_ast_operator(char const *const s, ast_operator *out) {

    static char const *const strings[] = {TESS_AST_OPERATOR_TAGS(MOS_TAG_STRING)};

    for (int i = 0; strings[i] != null; ++i) {
        if (0 == strcmp(strings[i], s)) {
            *out = (ast_operator)i;
            return 0;
        }
    }
    return 1;
}

void ast_vector_init(allocator *alloc, vector *vec) {
    vec_init(alloc, vec, sizeof(ast_node *), 0);
}

char *ast_node_to_string(allocator *alloc, ast_node const *node) {
    sexp  expr = ast_node_to_sexp(alloc, node);
    char *out  = sexp_to_string(alloc, expr);
    sexp_deinit(alloc, &expr);
    return out;
}
