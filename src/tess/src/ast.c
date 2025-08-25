#include "ast.h"
#include "alloc.h"
#include "dbg.h"
#include "mos_string.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
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

// -- pool operations --

void ast_pool_dfs(ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    switch (node->tag) {
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string: return fun(node);

    case ast_infix:
        ast_pool_dfs(node->infix.left, fun);
        ast_pool_dfs(node->infix.right, fun);
        return fun(node);

    case ast_tuple: {
        ast_node **it  = (ast_node **)vec_begin(&node->tuple.elements);
        ast_node **end = (ast_node **)vec_end(&node->tuple.elements);
        while (it != end) ast_pool_dfs(*it++, fun);

        return fun(node);
    } break;

    case ast_let_in:
        ast_pool_dfs(node->let_in.name, fun);
        ast_pool_dfs(node->let_in.value, fun);
        ast_pool_dfs(node->let_in.body, fun);

        return fun(node);

    case ast_let: {
        ast_pool_dfs(node->let.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->let.parameters);
        ast_node **end = (ast_node **)vec_end(&node->let.parameters);
        while (it != end) ast_pool_dfs(*it++, fun);

        ast_pool_dfs(node->let.body, fun);

        return fun(node);
    } break;

    case ast_if_then_else:
        ast_pool_dfs(node->if_then_else.condition, fun);
        ast_pool_dfs(node->if_then_else.yes, fun);
        ast_pool_dfs(node->if_then_else.no, fun);

        return fun(node);

    case ast_lambda_function: {
        ast_node **it  = (ast_node **)vec_begin(&node->lambda_function.parameters);
        ast_node **end = (ast_node **)vec_end(&node->lambda_function.parameters);
        while (it != end) ast_pool_dfs(*it++, fun);

        ast_pool_dfs(node->lambda_function.body, fun);

        return fun(node);
    } break;

    case ast_function_declaration: {
        ast_pool_dfs(node->function_declaration.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->function_declaration.parameters);
        ast_node **end = (ast_node **)vec_end(&node->function_declaration.parameters);
        while (it != end) ast_pool_dfs(*it++, fun);

        return fun(node);
    } break;

    case ast_lambda_declaration: {
        ast_node **it  = (ast_node **)vec_begin(&node->lambda_declaration.parameters);
        ast_node **end = (ast_node **)vec_end(&node->lambda_declaration.parameters);
        while (it != end) ast_pool_dfs(*it++, fun);

        return fun(node);

    } break;

    case ast_lambda_function_application: {
        ast_pool_dfs(node->lambda_application.lambda, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->lambda_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->lambda_application.arguments);
        while (it != end) ast_pool_dfs(*it++, fun);

        return fun(node);
    } break;

    case ast_named_function_application: {
        ast_pool_dfs(node->named_application.name, fun);

        ast_node **it  = (ast_node **)vec_begin(&node->named_application.arguments);
        ast_node **end = (ast_node **)vec_end(&node->named_application.arguments);
        while (it != end) ast_pool_dfs(*it++, fun);

        return fun(node);
    } break;
    }
}

void ast_pool_cdfs(ast_node const *start, ast_op_cfun fun) {
    ast_pool_dfs((ast_node *)start, (ast_op_fun)fun);
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

static int print_node(ast_node const *node, char *restrict buf, int const sz_,
                      char const *restrict literal) {
    if (sz_ < 0) return -1;
    size_t const sz = (size_t)sz_;

    if ((null == node) && (null != literal)) {
        return snprintf(buf, sz, "%s", literal);
    }

    if (null == node) {
        return snprintf(buf, sz, "NULL");
    }

    int offset = 0;

#define do_print_init() int res = 0

#define do_print_node(NODE)                                                                                \
    do {                                                                                                   \
        res = print_node(NODE, buf + offset, sz_ - offset, null);                                          \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

#define do_print_literal(LITERAL)                                                                          \
    do {                                                                                                   \
        res = print_node(null, buf + offset, sz_ - offset, LITERAL);                                       \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

#define do_print_list(FIELD)                                                                               \
    do {                                                                                                   \
        size_t           count = vec_size(&FIELD);                                                         \
        ast_node const **it    = (ast_node const **)vec_cbegin(&FIELD);                                    \
        while (count--) {                                                                                  \
            do_print_node(*it);                                                                            \
            if (count) do_print_literal(" ");                                                              \
                                                                                                           \
            ++it;                                                                                          \
        }                                                                                                  \
    } while (0)

    switch (node->tag) {

    case ast_eof:    return snprintf(buf, sz, "(eof)");
    case ast_nil:    return snprintf(buf, sz, "(nil)");
    case ast_bool:   return snprintf(buf, sz, "(bool %d)", node->bool_.val);
    case ast_symbol: return snprintf(buf, sz, "(symbol %s)", mos_string_str(&node->symbol.name));
    case ast_i64:    return snprintf(buf, sz, "(i64 %" PRId64 ")", node->i64.val);
    case ast_u64:    return snprintf(buf, sz, "(u64 %" PRIu64 ")", node->u64.val);
    case ast_f64:    return snprintf(buf, sz, "(f64 %f)", node->f64.val);
    case ast_string: return snprintf(buf, sz, "(string \"%s\")", mos_string_str(&node->symbol.name));

    case ast_infix:  {
        ast_node *left, *right;
        left  = node->infix.left;
        right = node->infix.right;

        do_print_init();

        res = snprintf(buf, sz, "(infix %s ", ast_operator_to_string(node->infix.op));
        if (res < 0) return res;
        offset += res;

        do_print_node(left);
        do_print_literal(" ");
        do_print_node(right);
        do_print_literal(")");

    } break;

    case ast_tuple: {
        do_print_init();

        res = snprintf(buf, sz, "(tuple ");
        if (res < 0) return 1;
        offset += res;

        do_print_list(node->tuple.elements);
        do_print_literal(")");

    } break;

    case ast_let_in: {
        ast_node *name, *value, *body;
        name  = node->let_in.name;
        value = node->let_in.value;
        body  = node->let_in.body;

        do_print_init();
        do_print_literal("(let_in ");
        do_print_node(name);
        do_print_literal(" ");
        do_print_node(value);
        do_print_literal(" ");
        do_print_node(body);
        do_print_literal(")");

    } break;

    case ast_let: {
        ast_node *name, *body;
        name = node->let.name;
        body = node->let.body;

        do_print_init();
        do_print_literal("(let ");
        do_print_node(name);
        do_print_literal(" (");
        do_print_list(node->let.parameters);
        do_print_literal(") ");
        do_print_node(body);
        do_print_literal(")");

    } break;

    case ast_if_then_else: {
        ast_node *cond, *yes, *no;
        cond = node->if_then_else.condition;
        yes  = node->if_then_else.yes;
        no   = node->if_then_else.no;

        do_print_init();
        do_print_literal("(if_then_else ");
        do_print_node(cond);
        do_print_literal(" ");
        do_print_node(yes);
        do_print_literal(" ");
        do_print_node(no);
        do_print_literal(")");

    } break;

    case ast_lambda_function: {
        ast_node *body = node->lambda_function.body;

        do_print_init();
        do_print_literal("(lambda (");
        do_print_list(node->lambda_function.parameters);
        do_print_literal(") ");
        do_print_node(body);
        do_print_literal(")");

    } break;

    case ast_function_declaration: {
        ast_node *name = node->function_declaration.name;

        do_print_init();
        do_print_literal("(function_declaration (");
        do_print_node(name);
        do_print_literal(" ");
        do_print_list(node->function_declaration.parameters);
        do_print_literal("))");

    } break;

    case ast_lambda_declaration: {

        do_print_init();
        do_print_literal("(lambda_declaration (");
        do_print_list(node->lambda_declaration.parameters);
        do_print_literal("))");

    } break;

    case ast_lambda_function_application: {
        ast_node *lambda = node->lambda_application.lambda;

        do_print_init();
        do_print_literal("(lambda_application ");
        do_print_node(lambda);
        do_print_literal(" ");
        do_print_list(node->lambda_application.arguments);
        do_print_literal(")");

    } break;
    case ast_named_function_application: {
        ast_node *name = node->named_application.name;

        do_print_init();
        do_print_literal("(application ");
        do_print_node(name);
        do_print_literal(" ");
        do_print_list(node->named_application.arguments);
        do_print_literal(")");

    } break;
    }

    return offset;

#undef do_print_node
#undef do_print_literal
}

int ast_node_to_string_buf(ast_node const *node, char *buf, size_t sz_) {
    if (sz_ > INT_MAX) return 1;
    int sz  = (int)sz_;

    int res = print_node(node, buf, sz, null);

    // check error conditions from snprintf
    if (res < 0 || res > sz) return 1;
    return 0;
}
