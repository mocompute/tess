#include "ast.h"
#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "dbg.h"
#include "mos_string.h"
#include "sexp.h"
#include "type.h"
#include "vector.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

ast_node *ast_node_create(allocator *alloc, ast_tag tag) {
    // FIXME this should probably be called alloc, because it doesn't
    // init the node like other _create functions do.
    ast_node *self = alloc_calloc(alloc, 1, sizeof *self);
    self->tag      = tag;
    return self;
}

ast_node *ast_node_create_sym(allocator *alloc, char const *str) {
    ast_node *self    = ast_node_create(alloc, ast_symbol);
    self->symbol.name = mos_string_init(alloc, str);
    return self;
}

void ast_node_move(ast_node *dst, ast_node *src) {
    alloc_copy(dst, src);
    alloc_zero(src); // valid nil node
}

nodiscard ast_node *ast_node_clone(allocator *alloc, ast_node const *orig) {

    if (null == orig) return null;

    ast_node *clone = ast_node_create(alloc, orig->tag);

    // types are copied by reference, not cloned
    clone->type = orig->type;

    // clone common array for some tags
    switch (clone->tag) {
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_let:
    case ast_tuple:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_user_type:                   {
        struct ast_array *vclone = ast_node_arr(clone), *vorig = ast_node_arr((ast_node *)orig);
        vclone->n     = vorig->n;
        vclone->nodes = alloc_malloc(alloc, vorig->n * sizeof vclone->nodes[0]);
        for (u32 i = 0; i < vclone->n; ++i) vclone->nodes[i] = ast_node_clone(alloc, vorig->nodes[i]);
    } break;

    case ast_user_type_definition:
    case ast_symbol:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_let_in:
    case ast_if_then_else:         break;
    }

    // clone the rest of the fields
    switch (clone->tag) {
    case ast_eof:
    case ast_nil:    break;
    case ast_bool:   clone->bool_.val = orig->bool_.val; break;

    case ast_i64:    clone->i64.val = orig->i64.val; break;
    case ast_u64:    clone->u64.val = orig->u64.val; break;
    case ast_f64:    clone->f64.val = orig->f64.val; break;

    case ast_symbol:
    case ast_string: {
        struct ast_symbol *vclone = ast_node_sym(clone), *vorig = ast_node_sym((ast_node *)orig);
        mos_string_copy(alloc, &vclone->name, &vorig->name);
        mos_string_copy(alloc, &vclone->original, &vorig->original);
        vclone->annotation = ast_node_clone(alloc, vorig->annotation);
    } break;

    case ast_infix: {
        struct ast_infix *vclone = ast_node_infix(clone), *vorig = ast_node_infix((ast_node *)orig);
        vclone->left  = ast_node_clone(alloc, vorig->left);
        vclone->right = ast_node_clone(alloc, vorig->right);
        vclone->op    = vorig->op;
    } break;

    case ast_tuple:  break;

    case ast_let_in: {
        struct ast_let_in *vclone = ast_node_let_in(clone), *vorig = ast_node_let_in((ast_node *)orig);
        vclone->name  = ast_node_clone(alloc, vorig->name);
        vclone->value = ast_node_clone(alloc, vorig->value);
        vclone->body  = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_let: {
        struct ast_let *vclone = ast_node_let(clone), *vorig = ast_node_let((ast_node *)orig);
        mos_string_copy(alloc, &vclone->name, &vorig->name);
        vclone->body  = ast_node_clone(alloc, vorig->body);
        vclone->arrow = vorig->arrow;
        mos_string_copy(alloc, &vclone->specialized_name, &vorig->specialized_name);
    } break;

    case ast_if_then_else: {
        struct ast_if_then_else *vclone = ast_node_ifthen(clone),
                                *vorig  = ast_node_ifthen((ast_node *)orig);
        vclone->condition               = ast_node_clone(alloc, vorig->condition);
        vclone->yes                     = ast_node_clone(alloc, vorig->yes);
        vclone->no                      = ast_node_clone(alloc, vorig->no);
    } break;

    case ast_lambda_function: {
        struct ast_lambda_function *vclone = ast_node_lf(clone), *vorig = ast_node_lf((ast_node *)orig);
        vclone->body = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_function_declaration: {
        struct ast_function_declaration *vclone = ast_node_fd(clone),
                                        *vorig  = ast_node_fd((ast_node *)orig);
        vclone->name                            = ast_node_clone(alloc, vorig->name);
    } break;

    case ast_lambda_declaration:          break;

    case ast_lambda_function_application: {
        struct ast_lambda_application *vclone = ast_node_lamda(clone),
                                      *vorig  = ast_node_lamda((ast_node *)orig);
        vclone->lambda                        = ast_node_clone(alloc, vorig->lambda);
    } break;

    case ast_named_function_application: {
        struct ast_named_application *vclone = ast_node_named(clone),
                                     *vorig  = ast_node_named((ast_node *)orig);
        mos_string_copy(alloc, &vclone->name, &vorig->name);
        vclone->specialized = ast_node_clone(alloc, vorig->specialized);
    } break;

    case ast_user_type: {
        struct ast_user_type *vclone = ast_node_ut(clone), *vorig = ast_node_ut((ast_node *)orig);
        vclone->name = ast_node_clone(alloc, vorig->name);
    } break;

    case ast_user_type_definition: {
        struct ast_user_type_def *vclone = ast_node_utd(clone), *vorig = ast_node_utd((ast_node *)orig);

        vclone->name        = ast_node_clone(alloc, vorig->name);

        vclone->n_fields    = vorig->n_fields;
        vclone->field_types = vorig->field_types; // always in arena, never clone types

        for (u32 i = 0; i < vclone->n_fields; ++i) {
            vclone->field_annotations[i] = ast_node_clone(alloc, vorig->field_annotations[i]);
            vclone->field_names[i]       = ast_node_clone(alloc, vorig->field_names[i]);
        }
    } break;
    }

    return clone;
}

char const *ast_node_name_string(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag)
        fatal("ast_node_name_string: expected symbol or string");

    return mos_string_str(&node->symbol.name);
}

int ast_node_name_strcmp(ast_node const *node, char const *target) {
    char const *name = ast_node_name_string(node);
    if (!name) return false;
    return strcmp(name, target);
}

char const *ast_operator_to_string(ast_operator);

//

sexp        do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                                sexp (*symbol_fun)(allocator *, ast_node const *));

static sexp elements_to_sexp(allocator *alloc, struct ast_node **elements, u16 const n,
                             sexp (*symbol_fun)(allocator *, ast_node const *)) {

    sexp *sexp_elements = alloc_malloc(alloc, sizeof(sexp) * n);

    for (size_t i = 0; i < n; ++i) sexp_elements[i] = do_ast_node_to_sexp(alloc, elements[i], symbol_fun);

    sexp list = sexp_init_list(alloc, sexp_elements, n);

    alloc_free(alloc, sexp_elements);
    return list;
}

sexp symbol_node_to_sexp(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    sexp type;
    {
        int  len = tl_type_snprint(null, 0, node->type) + 1;
        char buf[len];
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym(alloc, buf);
    }
    return sexp_init_list_triple(alloc, sexp_init_sym(alloc, "symbol"),
                                 sexp_init_sym(alloc, ast_node_name_string(node)), type);
}

sexp symbol_node_to_sexp_for_error(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    sexp type;
    {
        int  len = tl_type_snprint(null, 0, node->type) + 1;
        char buf[len];
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym(alloc, buf);
    }

    if (!mos_string_empty(&node->symbol.original))
        return sexp_init_list_triple(alloc, sexp_init_sym(alloc, "symbol"),
                                     sexp_init_sym(alloc, mos_string_str(&node->symbol.original)), type);
    else
        return sexp_init_list_triple(alloc, sexp_init_sym(alloc, "symbol"),
                                     sexp_init_sym(alloc, mos_string_str(&node->symbol.name)), type);
}

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *)) {

#define pair(...)   sexp_init_list_pair(__VA_ARGS__)
#define triple(...) sexp_init_list_triple(__VA_ARGS__)
#define quad(...)   sexp_init_list_quad(__VA_ARGS__)
#define penta(...)  sexp_init_list_penta(__VA_ARGS__)
#define recur(NODE) do_ast_node_to_sexp(alloc, (NODE), symbol_fun)
#define sym(STR)    sexp_init_sym(alloc, (STR))

    if (null == node) return sexp_init_boxed(alloc);

    sexp type;
    if (node->tag != ast_symbol) // symbols are delegated to symbol_fun
    {
        int  len = tl_type_snprint(null, 0, node->type) + 1;
        char buf[len];
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym(alloc, buf);
    }

    switch (node->tag) {

    case ast_eof:    return pair(alloc, sym("eof"), type);
    case ast_nil:    return pair(alloc, sym("nil"), type);
    case ast_bool:   return pair(alloc, node->bool_.val ? sym("true") : sym("false"), type);

    case ast_symbol: return symbol_fun(alloc, node);

    case ast_i64:    return triple(alloc, sym("i64"), sexp_init_i64(alloc, node->i64.val), type);
    case ast_u64:    return triple(alloc, sym("u64"), sexp_init_u64(alloc, node->u64.val), type);
    case ast_f64:    return triple(alloc, sym("f64"), sexp_init_f64(alloc, node->f64.val), type);

    case ast_string: return triple(alloc, sym("string"), sym(ast_node_name_string(node)), type);

    case ast_infix:
        return penta(alloc, sym("infix"), sym(ast_operator_to_string(node->infix.op)),
                     recur(node->infix.left), recur(node->infix.right), type);

    case ast_tuple: {

        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("tuple"), list, type);

    } break;

    case ast_let_in:
        return penta(alloc, sym("let-in"), recur(node->let_in.name), recur(node->let_in.value),
                     recur(node->let_in.body), type);

    case ast_let: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        if (mos_string_empty(&node->let.specialized_name))
            return penta(alloc, sym("let"), sym(mos_string_str(&node->let.name)), list,
                         recur(node->let.body), type);
        else
            return penta(alloc, sym("let"), sym(mos_string_str(&node->let.specialized_name)), list,
                         recur(node->let.body), type);

    } break;

    case ast_if_then_else:
        return penta(alloc, sym("if-then-else"), recur(node->if_then_else.condition),
                     recur(node->if_then_else.yes), recur(node->if_then_else.no), type);

    case ast_lambda_function: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return quad(alloc, sym("lambda"), list, recur(node->lambda_function.body), type);

    } break;

    case ast_function_declaration: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return quad(alloc, sym("function-declaration"), list, recur(node->lambda_function.body), type);

    } break;

    case ast_lambda_declaration: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("lambda-declaration"), list, type);

    } break;

    case ast_lambda_function_application: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return quad(alloc, sym("lambda-application"), recur(node->lambda_application.lambda), list, type);

    } break;
    case ast_named_function_application: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        if (node->named_application.specialized)
            return quad(alloc, sym("named-application"),
                        sym(mos_string_str(&node->named_application.specialized->let.specialized_name)),
                        list, type);
        else
            return quad(alloc, sym("named-application"), sym(mos_string_str(&node->named_application.name)),
                        list, type);

    } break;

    case ast_user_type: {
        u16   n        = node->user_type.n_fields;
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * n);
        for (size_t i = 0; i < n; ++i) elements[i] = recur(node->user_type.fields[i]);
        sexp field_list = sexp_init_list(alloc, elements, n);
        alloc_free(alloc, elements);
        return quad(alloc, sym("user-type"), recur(node->user_type.name), field_list, type);

    } break;

    case ast_user_type_definition: {
        u16        n                 = node->user_type_def.n_fields;
        ast_node **field_names       = node->user_type_def.field_names;
        ast_node **field_annotations = node->user_type_def.field_annotations;

        sexp      *sexp_elements     = alloc_malloc(alloc, sizeof(sexp) * n);

        for (size_t i = 0; i < n; ++i) sexp_elements[i] = recur(field_names[i]);
        sexp names_list = sexp_init_list(alloc, sexp_elements, n);

        for (size_t i = 0; i < n; ++i) sexp_elements[i] = recur(field_annotations[i]);

        sexp annotations_list = sexp_init_list(alloc, sexp_elements, n);

        alloc_free(alloc, sexp_elements);
        return penta(alloc, sym("def-user-type"), recur(node->user_type_def.name), names_list,
                     annotations_list, type);

    } break;
    }

#undef pair
#undef triple
#undef quad
#undef penta
#undef recur
#undef sym
}

sexp ast_node_to_sexp(allocator *alloc, ast_node const *node) {
    return do_ast_node_to_sexp(alloc, node, symbol_node_to_sexp);
}

sexp ast_node_to_sexp_for_error(allocator *alloc, ast_node const *node) {
    return do_ast_node_to_sexp(alloc, node, symbol_node_to_sexp_for_error);
}

void map_ast_node_to_sexp(void *alloc, void *out, void const *node_ptr) {
    *(sexp *)out = ast_node_to_sexp(alloc, (ast_node const *)node_ptr);
}

sexp ast_node_to_sexp_with_type(allocator *alloc, ast_node const *node) {

    sexp expr = ast_node_to_sexp(alloc, node);

    int  len  = tl_type_snprint(null, 0, node->type) + 1;
    char buf[len];
    tl_type_snprint(buf, len, node->type);

    sexp list = sexp_init_list_pair(alloc, expr, sexp_init_sym(alloc, buf));

    return list;
}

// -- pool operations --

static void recur_on_array(struct ast_node **elements, u16 n, void *ctx, ast_op_fun fun) {
    for (size_t i = 0; i < n; ++i) ast_node_dfs(ctx, elements[i], fun);
}

void ast_node_dfs(void *ctx, ast_node *node, ast_op_fun fun) {

    if (!node) return;

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
        ast_node_dfs(ctx, node->infix.left, fun);
        ast_node_dfs(ctx, node->infix.right, fun);
        return fun(ctx, node);

    case ast_tuple: {
        recur_on_array(node->array.nodes, node->array.n, ctx, fun);
        return fun(ctx, node);
    } break;

    case ast_let_in:
        ast_node_dfs(ctx, node->let_in.name, fun);
        ast_node_dfs(ctx, node->let_in.value, fun);
        ast_node_dfs(ctx, node->let_in.body, fun);

        return fun(ctx, node);

    case ast_let: {

        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        ast_node_dfs(ctx, node->let.body, fun);

        return fun(ctx, node);
    } break;

    case ast_if_then_else:
        ast_node_dfs(ctx, node->if_then_else.condition, fun);
        ast_node_dfs(ctx, node->if_then_else.yes, fun);
        ast_node_dfs(ctx, node->if_then_else.no, fun);

        return fun(ctx, node);

    case ast_lambda_function: {
        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        ast_node_dfs(ctx, node->lambda_function.body, fun);

        return fun(ctx, node);
    } break;

    case ast_function_declaration: {
        ast_node_dfs(ctx, node->function_declaration.name, fun);

        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        return fun(ctx, node);
    } break;

    case ast_lambda_declaration: {
        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        return fun(ctx, node);

    } break;

    case ast_lambda_function_application: {
        ast_node_dfs(ctx, node->lambda_application.lambda, fun);

        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        return fun(ctx, node);
    } break;

    case ast_named_function_application: {
        ast_node_dfs(ctx, node->named_application.specialized, fun);

        recur_on_array(node->array.nodes, node->array.n, ctx, fun);

        return fun(ctx, node);
    } break;

    case ast_user_type: {
        ast_node_dfs(ctx, node->user_type.name, fun);
        recur_on_array(node->array.nodes, node->array.n, ctx, fun);
        return fun(ctx, node);
    } break;

    case ast_user_type_definition:
        // excluded from dfs
        return;
    }
    assert(false);
}

void ast_node_cdfs(void *ctx, ast_node const *start, ast_op_cfun fun) {
    ast_node_dfs(ctx, (ast_node *)start, (ast_op_fun)fun);
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

char *ast_node_to_string(allocator *alloc, ast_node const *node) {
    sexp  expr = ast_node_to_sexp(alloc, node);
    char *out  = sexp_to_string(alloc, expr);
    sexp_deinit(alloc, &expr);
    return out;
}

char *ast_node_to_string_for_error(allocator *alloc, ast_node const *node) {
    sexp  expr = ast_node_to_sexp_for_error(alloc, node);
    char *out  = sexp_to_string(alloc, expr);
    sexp_deinit(alloc, &expr);
    return out;
}

c_string_csized ast_nodes_get_names(allocator *alloc, ast_node_slice nodes) {

    c_string_csized strings = {.v    = alloc_calloc(alloc, nodes.end - nodes.begin, sizeof strings.v[0]),
                               .size = nodes.end - nodes.begin};

    for (u32 i = nodes.begin; i < nodes.end; ++i)
        strings.v[i - nodes.begin] = ast_node_name_string(nodes.v[i]);

    return strings;
}

static void validate_one_node(void *ctx, ast_node *node) {
    (void)ctx;
    bool valid = false;
    switch (node->tag) {
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_infix:
    case ast_tuple:
    case ast_let_in:
    case ast_let:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:
    case ast_user_type:
    case ast_user_type_definition:        valid = true; break;
    }
    if (!valid) {

        dbg("found invalid node at %p\n", node);
        assert(valid);
    }
}

void ast_validate_nodes(ast_node *nodes[], u32 count) {

    for (size_t i = 0; i < count; ++i) {
        ast_node_dfs(null, nodes[i], validate_one_node);
    }

    dbg("all nodes valid\n");
}

struct ast_symbol *ast_node_sym(ast_node *node) {
    assert(node->tag == ast_symbol || node->tag == ast_string);
    return &node->symbol;
}

struct ast_bool *ast_node_bool(ast_node *node) {
    assert(node->tag == ast_bool);
    return &node->bool_;
}

struct ast_i64 *ast_node_i64(ast_node *node) {
    assert(node->tag == ast_i64);
    return &node->i64;
}

struct ast_u64 *ast_node_u64(ast_node *node) {
    assert(node->tag == ast_u64);
    return &node->u64;
}

struct ast_f64 *ast_node_f64(ast_node *node) {
    assert(node->tag == ast_f64);
    return &node->f64;
}

struct ast_array *ast_node_arr(ast_node *node) {
    switch (node->tag) {
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
    case ast_if_then_else:
    case ast_user_type_definition:
        assert(false);
        fatal("ast_node_arr called on non-array variant");
        break;

    case ast_user_type:
    case ast_tuple:
    case ast_let:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application:
    case ast_named_function_application:  return &node->array;
    }
}

struct ast_user_type *ast_node_ut(ast_node *node) {
    assert(node->tag == ast_user_type);
    return &node->user_type;
}

struct ast_infix *ast_node_infix(ast_node *node) {
    assert(node->tag == ast_infix);
    return &node->infix;
}

struct ast_lambda_function *ast_node_lf(ast_node *node) {
    assert(node->tag == ast_lambda_function);
    return &node->lambda_function;
}

struct ast_let_in *ast_node_let_in(ast_node *node) {
    assert(node->tag == ast_let_in);
    return &node->let_in;
}

struct ast_function_declaration *ast_node_fd(ast_node *node) {
    assert(node->tag == ast_function_declaration);
    return &node->function_declaration;
}

struct ast_lambda_declaration *ast_node_let_ld(ast_node *node) {
    assert(node->tag == ast_lambda_declaration);
    return &node->lambda_declaration;
}

struct ast_let *ast_node_let(ast_node *node) {
    assert(node->tag == ast_let);
    return &node->let;
}

struct ast_if_then_else *ast_node_ifthen(ast_node *node) {
    assert(node->tag == ast_if_then_else);
    return &node->if_then_else;
}

struct ast_lambda_application *ast_node_lamda(ast_node *node) {
    assert(node->tag == ast_lambda_function_application);
    return &node->lambda_application;
}

struct ast_named_application *ast_node_named(ast_node *node) {
    assert(node->tag == ast_named_function_application);
    return &node->named_application;
}

struct ast_tuple *ast_node_tuple(ast_node *node) {
    assert(node->tag == ast_tuple);
    return &node->tuple;
}

struct ast_user_type_def *ast_node_utd(ast_node *node) {
    assert(node->tag == ast_user_type_definition);
    return &node->user_type_def;
}
