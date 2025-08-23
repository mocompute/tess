#include "ast.h"
#include "alloc.h"
#include "util.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

// -- statics --

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type *ty, type_tag tag) {
    alloc_zero(ty);
    ty->tag = tag;
}

void tess_type_init_type_var(tess_type *ty, u32 val) {
    alloc_zero(ty);
    ty->tag = type_type_var;
    ty->val = val;
}

int tess_type_init_tuple(allocator *alloc, tess_type *ty) {
    alloc_zero(ty);
    ty->tag = type_tuple;
    return vec_init(alloc, &ty->tuple, sizeof(ast_node_h), 0);
}

void tess_type_init_arrow(tess_type *ty) {
    alloc_zero(ty);
    ty->tag = type_arrow;
}

void tess_type_deinit(allocator *alloc, tess_type *ty) {
    switch (ty->tag) {
    case type_nil:
    case type_bool:
    case type_int:
    case type_float:
    case type_arrow:
    case type_type_var:
    case type_string:   break;
    case type_tuple:    vec_deinit(alloc, &ty->tuple); break;
    }
    alloc_invalidate(ty);
}

// -- tess_type_pool allocation and deallocation --

ast_pool *ast_pool_alloc(allocator *alloc) {
    return alloc->malloc(alloc, sizeof(ast_pool));
}

ast_pool *ast_pool_create(allocator *alloc) {
    ast_pool *out = alloc->malloc(alloc, sizeof(ast_pool));
    if (!out) return out;

    if (ast_pool_init(alloc, out)) {
        alloc->free(alloc, out);
        return null;
    }
    return out;
}

void ast_pool_dealloc(allocator *alloc, ast_pool **pool) {
    alloc_assert_invalid(*pool, sizeof *pool);
    alloc->free(alloc, *pool);
    *pool = null;
}

void ast_pool_destroy(allocator *alloc, ast_pool **pool) {
    ast_pool_deinit(alloc, *pool);
    ast_pool_dealloc(alloc, pool);
}

int ast_pool_init(allocator *alloc, ast_pool *pool) {
    return vec_init(alloc, &pool->data, sizeof(ast_node), 32);
}

void ast_pool_deinit(allocator *alloc, ast_pool *pool) {
    // deinit all the ast nodes
    ast_node       *it  = vec_begin(&pool->data);
    ast_node const *end = vec_end(&pool->data);
    while (it != end) {
        ast_node_deinit(alloc, it++);
    }

    vec_deinit(alloc, &pool->data);
    alloc_invalidate(pool);
}

// -- ast_node init and deinit --

void ast_node_deinit(allocator *alloc, ast_node *node) {

#define deinit(P) vec_deinit(alloc, &P)

    switch (node->tag) {
    case ast_lambda_function:             deinit(node->lambda_function.parameters); break;
    case ast_function_declaration:        deinit(node->function_declaration.parameters); break;
    case ast_lambda_declaration:          deinit(node->lambda_declaration.parameters); break;
    case ast_let:                         deinit(node->let.parameters); break;
    case ast_tuple:                       deinit(node->tuple.elements); break;
    case ast_lambda_function_application: deinit(node->lambda_function_application.arguments); break;
    case ast_named_function_application:  deinit(node->named_function_application.arguments); break;
    case ast_symbol:
        if (node->symbol.name) {
            // TODO: intern or pool strings
            alloc->free(alloc, node->symbol.name);
            node->symbol.name = 0;
        }
        break;
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_let_in:
    case ast_if_then_else: break;
    }

#undef deinit
}

int ast_node_init(allocator *alloc, ast_node *node, ast_tag tag) {

    // accepts alloc = null in some cases

#define init(P)                                                                                            \
    do {                                                                                                   \
        assert(alloc);                                                                                     \
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
    case ast_lambda_function_application: init(node->lambda_function_application.arguments);
    case ast_named_function_application:  init(node->named_function_application.arguments);

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
    case ast_if_then_else:                return 0;
    }

#undef init
}

int ast_node_replace(allocator *alloc, ast_node *node, ast_tag tag) {
    ast_node_deinit(alloc, node);
    return ast_node_init(alloc, node, tag);
}

// -- pool operations --

int ast_pool_move_back(allocator *alloc, ast_pool *pool, ast_node *node, ast_node_h *handle) {

    if (vec_push_back(alloc, &pool->data, node)) return 1;

    handle->val = vec_size(&pool->data) - 1;
    alloc_invalidate(node);

    return 0;
}

ast_node *ast_pool_at(ast_pool *pool, ast_node_h handle) {
    return vec_at(&pool->data, handle.val);
}

// -- utilities --

char const *type_tag_to_string(type_tag tag) {
    static char const *const strings[] = {TESS_TYPE_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}

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

int ast_vector_init(allocator *alloc, vec_t *vec) {
    return vec_init(alloc, vec, sizeof(ast_node_h), 0);
}

static int print_node(ast_pool *pool, ast_node const *node, char *restrict buf, int const sz_,
                      char const *restrict literal) {
    if (sz_ < 0) return -1;
    size_t const sz = (size_t)sz_;

    if ((null == node) && (null != literal)) {
        return snprintf(buf, sz, "%s", literal);
    }

#define do_print_init()                                                                                    \
    int res    = 0;                                                                                        \
    int offset = 0

#define do_print_node(NODE)                                                                                \
    do {                                                                                                   \
        res = print_node(pool, NODE, buf + offset, sz_ - offset, null);                                    \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

#define do_print_literal(LITERAL)                                                                          \
    do {                                                                                                   \
        res = print_node(pool, null, buf + offset, sz_ - offset, LITERAL);                                 \
        if (res < 0) return res;                                                                           \
        offset += res;                                                                                     \
    } while (0)

#define do_print_list(FIELD)                                                                               \
    do {                                                                                                   \
        size_t            count = vec_size(&FIELD);                                                        \
        ast_node_h const *it    = vec_cbegin(&FIELD);                                                      \
        while (count--) {                                                                                  \
                                                                                                           \
            ast_node *el = ast_pool_at(pool, *it);                                                         \
            do_print_node(el);                                                                             \
            if (count) do_print_literal(" ");                                                              \
                                                                                                           \
            ++it;                                                                                          \
        }                                                                                                  \
    } while (0)

    switch (node->tag) {

    case ast_eof:    return snprintf(buf, sz, "(eof)");
    case ast_nil:    return snprintf(buf, sz, "(nil)");
    case ast_bool:   return snprintf(buf, sz, "(bool %d)", node->bool_.val);
    case ast_symbol: return snprintf(buf, sz, "(symbol %s)", node->symbol.name);
    case ast_i64:    return snprintf(buf, sz, "(i64 %" PRId64 ")", node->i64.val);
    case ast_u64:    return snprintf(buf, sz, "(u64 %" PRIu64 ")", node->u64.val);
    case ast_f64:    return snprintf(buf, sz, "(f64 %f)", node->f64.val);
    case ast_string: return snprintf(buf, sz, "(string \"%s\")", node->symbol.name);

    case ast_infix:  {
        ast_node *left, *right;
        left  = ast_pool_at(pool, node->infix.left);
        right = ast_pool_at(pool, node->infix.right);

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
        ast_node *name, *body, *value;
        name  = ast_pool_at(pool, node->let_in.name);
        body  = ast_pool_at(pool, node->let_in.body);
        value = ast_pool_at(pool, node->let_in.value);

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
        name = ast_pool_at(pool, node->let.name);
        body = ast_pool_at(pool, node->let.body);

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
        cond = ast_pool_at(pool, node->if_then_else.condition);
        yes  = ast_pool_at(pool, node->if_then_else.yes);
        no   = ast_pool_at(pool, node->if_then_else.no);

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
        ast_node *body = ast_pool_at(pool, node->lambda_function.body);

        do_print_init();
        do_print_literal("(lambda (");
        do_print_list(node->lambda_function.parameters);
        do_print_literal(") ");
        do_print_node(body);
        do_print_literal(")");

    } break;

    case ast_function_declaration: {
        ast_node *name = ast_pool_at(pool, node->function_declaration.name);

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
        ast_node *lambda = ast_pool_at(pool, node->lambda_function_application.lambda);

        do_print_init();
        do_print_literal("(lambda_application ");
        do_print_node(lambda);
        do_print_literal(" ");
        do_print_list(node->lambda_function_application.arguments);
        do_print_literal(")");

    } break;
    case ast_named_function_application: {
        ast_node *name = ast_pool_at(pool, node->named_function_application.name);

        do_print_init();
        do_print_literal("(application ");
        do_print_node(name);
        do_print_literal(" ");
        do_print_list(node->named_function_application.arguments);
        do_print_literal(")");

    } break;
    }

    return 0;

#undef do_print_node
#undef do_print_literal
}

int ast_node_to_string_buf(ast_pool *pool, ast_node const *node, char *buf, size_t sz_) {
    if (sz_ > INT_MAX) return 1;
    int sz  = (int)sz_;

    int res = print_node(pool, node, buf, sz, null);

    // check error conditions from snprintf
    if (res < 0 || res > sz) return 1;
    return 0;
}
