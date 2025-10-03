#include "ast.h"
#include "alloc.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hash.h"
#include "hashmap.h"
#include "sexp.h"
#include "string_t.h"
#include "type.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

ast_node *ast_node_create(allocator *alloc, ast_tag tag) {
    // FIXME this should probably be called alloc, because it doesn't
    // init the node like other _create functions do.
    ast_node *self = alloc_calloc(alloc, 1, sizeof *self);

    self->file     = "";
    self->line     = -1;
    self->type     = null;
    self->type_v2  = null;
    self->error    = tl_err_ok;

    self->tag      = tag;

    return self;
}

ast_node *ast_node_create_sym_c(allocator *alloc, char const *str) {
    ast_node *self                  = ast_node_create(alloc, ast_symbol);
    self->symbol.name               = str_init(alloc, str);
    self->symbol.original           = str_empty();
    self->symbol.annotation         = null;
    self->symbol.annotation_type    = null;
    self->symbol.annotation_type_v2 = null;
    self->symbol.special_hash       = 0;
    self->symbol.flags              = 0;
    return self;
}

ast_node *ast_node_create_sym(allocator *alloc, str str) {
    ast_node *self                  = ast_node_create(alloc, ast_symbol);
    self->symbol.name               = str_copy(alloc, str);
    self->symbol.original           = str_empty();
    self->symbol.annotation         = null;
    self->symbol.annotation_type    = null;
    self->symbol.annotation_type_v2 = null;
    self->symbol.special_hash       = 0;
    self->symbol.flags              = 0;
    return self;
}

void ast_node_move(ast_node *dst, ast_node *src) {
    alloc_copy(dst, src);
    alloc_zero(src); // valid nil node
}

nodiscard ast_node *ast_node_clone(allocator *alloc, ast_node const *orig) {

    if (null == orig) return null;

    ast_node *clone = ast_node_create(alloc, orig->tag);

    clone->file     = orig->file;
    clone->line     = orig->line;

    clone->type     = orig->type ? tl_type_clone_shallow(alloc, orig->type) : null;

    // clone common array for some tags
    if (TL_AST_HAS_ARRAY(clone->tag)) {
        struct ast_array *vclone = ast_node_arr(clone), *vorig = ast_node_arr((ast_node *)orig);
        vclone->n     = vorig->n;
        vclone->flags = vorig->flags;
        vclone->nodes = alloc_malloc(alloc, vorig->n * sizeof vclone->nodes[0]);
        for (u32 i = 0; i < vclone->n; ++i) vclone->nodes[i] = ast_node_clone(alloc, vorig->nodes[i]);
    }

    // clone the rest of the fields
    switch (clone->tag) {
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_any:
    case ast_begin_end:
    case ast_lambda_declaration:
    case ast_labelled_tuple:
    case ast_tuple:              break;

    case ast_bool:               clone->bool_.val = orig->bool_.val; break;
    case ast_i64:                clone->i64.val = orig->i64.val; break;
    case ast_u64:                clone->u64.val = orig->u64.val; break;
    case ast_f64:                clone->f64.val = orig->f64.val; break;

    case ast_address_of:         {
        struct ast_address_of *vclone = ast_node_address_of(clone),
                              *vorig  = ast_node_address_of((ast_node *)orig);
        vclone->target                = ast_node_clone(alloc, vorig->target);
    } break;

    case ast_arrow: {
        struct ast_arrow *vclone = ast_node_arrow(clone), *vorig = ast_node_arrow((ast_node *)orig);
        vclone->left  = ast_node_clone(alloc, vorig->left);
        vclone->right = ast_node_clone(alloc, vorig->right);
    } break;

    case ast_assignment: {
        struct ast_assignment *vclone = ast_node_assignment(clone),
                              *vorig  = ast_node_assignment((ast_node *)orig);
        vclone->name                  = ast_node_clone(alloc, vorig->name);
        vclone->value                 = ast_node_clone(alloc, vorig->value);
    } break;

    case ast_dereference: {
        struct ast_dereference *vclone = ast_node_deref(clone), *vorig = ast_node_deref((ast_node *)orig);
        vclone->target = ast_node_clone(alloc, vorig->target);
    } break;

    case ast_dereference_assign: {
        struct ast_dereference_assign *vclone = ast_node_deref_assign(clone),
                                      *vorig  = ast_node_deref_assign((ast_node *)orig);
        vclone->target                        = ast_node_clone(alloc, vorig->target);
        vclone->value                         = ast_node_clone(alloc, vorig->value);
    } break;

    case ast_symbol:
    case ast_string: {
        struct ast_symbol *vclone = ast_node_sym(clone), *vorig = ast_node_sym((ast_node *)orig);
        vclone->name            = str_copy(alloc, vorig->name);
        vclone->original        = str_copy(alloc, vorig->original);
        vclone->annotation      = ast_node_clone(alloc, vorig->annotation);
        vclone->annotation_type = tl_type_clone_shallow(alloc, vorig->annotation_type);
        vclone->special_hash    = vorig->special_hash;
        vclone->flags           = vorig->flags;
    } break;

    case ast_let_in: {
        struct ast_let_in *vclone = ast_node_let_in(clone), *vorig = ast_node_let_in((ast_node *)orig);
        vclone->name  = ast_node_clone(alloc, vorig->name);
        vclone->value = ast_node_clone(alloc, vorig->value);
        vclone->body  = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_let_match_in: {
        struct ast_let_match_in *vclone = ast_node_let_match_in(clone),
                                *vorig  = ast_node_let_match_in((ast_node *)orig);
        vclone->lt                      = ast_node_clone(alloc, vorig->lt);
        vclone->value                   = ast_node_clone(alloc, vorig->value);
        vclone->body                    = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_let: {
        struct ast_let *vclone = ast_node_let(clone), *vorig = ast_node_let((ast_node *)orig);
        vclone->flags = vorig->flags;
        vclone->name  = ast_node_clone(alloc, vorig->name);
        vclone->body  = ast_node_clone(alloc, vorig->body);
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
        vclone->flags = vorig->flags;
        vclone->body  = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_function_declaration: {
        struct ast_function_declaration *vclone = ast_node_fd(clone),
                                        *vorig  = ast_node_fd((ast_node *)orig);
        vclone->name                            = ast_node_clone(alloc, vorig->name);
    } break;

    case ast_lambda_function_application: {
        struct ast_lambda_application *vclone = ast_node_lambda(clone),
                                      *vorig  = ast_node_lambda((ast_node *)orig);
        vclone->lambda                        = ast_node_clone(alloc, vorig->lambda);
    } break;

    case ast_named_function_application: {
        struct ast_named_application *vclone = ast_node_named(clone),
                                     *vorig  = ast_node_named((ast_node *)orig);
        vclone->name                         = ast_node_clone(alloc, vorig->name);
        vclone->function_type                = tl_type_clone_shallow(alloc, vorig->function_type);
    } break;

    case ast_user_type: {
        struct ast_user_type *vclone = ast_node_ut(clone), *vorig = ast_node_ut((ast_node *)orig);
        vclone->name = ast_node_clone(alloc, vorig->name);
    } break;

    case ast_user_type_get: {
        struct ast_user_type_get *vclone = ast_node_utg(clone), *vorig = ast_node_utg((ast_node *)orig);
        vclone->struct_name = ast_node_clone(alloc, vorig->struct_name);
        vclone->field_name  = ast_node_clone(alloc, vorig->field_name);
    } break;

    case ast_user_type_set: {
        struct ast_user_type_set *vclone = ast_node_uts(clone), *vorig = ast_node_uts((ast_node *)orig);
        vclone->struct_name = ast_node_clone(alloc, vorig->struct_name);
        vclone->field_name  = ast_node_clone(alloc, vorig->field_name);
        vclone->value       = ast_node_clone(alloc, vorig->value);
    } break;

    case ast_user_type_definition: {
        struct ast_user_type_def *vclone = ast_node_utd(clone), *vorig = ast_node_utd((ast_node *)orig);

        vclone->name        = ast_node_clone(alloc, vorig->name);

        vclone->n_fields    = vorig->n_fields;
        vclone->field_types = vorig->field_types; // always in arena, never clone types
        // FIXME: need to shallow clone types?

        for (u32 i = 0; i < vclone->n_fields; ++i) {
            vclone->field_annotations[i] = ast_node_clone(alloc, vorig->field_annotations[i]);
            vclone->field_names[i]       = ast_node_clone(alloc, vorig->field_names[i]);
        }
    } break;
    }

    return clone;
}

str ast_node_str(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    return node->symbol.name;
}

str ast_node_name_original(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    if (str_is_empty(node->symbol.original)) return node->symbol.name;
    else return node->symbol.original;
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

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *));

sexp symbol_node_to_sexp(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    sexp type;
    {
        int   len = tl_type_snprint(null, 0, node->type) + 1;
        char *buf = alloc_malloc(alloc, len);
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym_c(alloc, buf);
        alloc_free(alloc, buf);
    }
    return sexp_init_list_quad(alloc, sexp_init_sym_c(alloc, "symbol"),
                               sexp_init_sym(alloc, ast_node_str(node)),
                               sexp_init_u64(alloc, node->symbol.special_hash), type);
}

sexp symbol_node_to_sexp_for_error(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    sexp type;
    {
        int   len = tl_type_snprint(null, 0, node->type) + 1;
        char *buf = alloc_malloc(alloc, len);
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym_c(alloc, buf);
        alloc_free(alloc, buf);
    }

    if (!str_is_empty(node->symbol.original))
        return sexp_init_list_triple(alloc, sexp_init_sym_c(alloc, "symbol"),
                                     sexp_init_sym(alloc, node->symbol.original), type);
    else
        return sexp_init_list_triple(alloc, sexp_init_sym_c(alloc, "symbol"),
                                     sexp_init_sym(alloc, node->symbol.name), type);
}

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *)) {

#define pair(...)   sexp_init_list_pair(__VA_ARGS__)
#define triple(...) sexp_init_list_triple(__VA_ARGS__)
#define quad(...)   sexp_init_list_quad(__VA_ARGS__)
#define penta(...)  sexp_init_list_penta(__VA_ARGS__)
#define recur(NODE) do_ast_node_to_sexp(alloc, (NODE), symbol_fun)
#define sym(STR)    sexp_init_sym_c(alloc, (STR))
#define symstr(STR) sexp_init_sym(alloc, (STR))

    if (null == node) return sexp_init_boxed(alloc);

    sexp type;
    if (node->tag != ast_symbol) // symbols are delegated to symbol_fun
    {
        int   len = tl_type_snprint(null, 0, node->type) + 1;
        char *buf = alloc_malloc(alloc, len);
        tl_type_snprint(buf, len, node->type);
        type = sexp_init_sym_c(alloc, buf);
        alloc_free(alloc, buf);
    }

    switch (node->tag) {

    case ast_ellipsis:    return pair(alloc, sym("..."), type);
    case ast_eof:         return pair(alloc, sym("eof"), type);
    case ast_nil:         return pair(alloc, sym("nil"), type);
    case ast_any:         return pair(alloc, sym("any"), type);
    case ast_bool:        return pair(alloc, node->bool_.val ? sym("true") : sym("false"), type);

    case ast_symbol:      return symbol_fun(alloc, node);

    case ast_i64:         return triple(alloc, sym("i64"), sexp_init_i64(alloc, node->i64.val), type);
    case ast_u64:         return triple(alloc, sym("u64"), sexp_init_u64(alloc, node->u64.val), type);
    case ast_f64:         return triple(alloc, sym("f64"), sexp_init_f64(alloc, node->f64.val), type);

    case ast_string:      return triple(alloc, sym("string"), symstr(ast_node_str(node)), type);

    case ast_address_of:  return triple(alloc, sym("&"), recur(node->address_of.target), type);
    case ast_arrow:       return quad(alloc, recur(node->arrow.left), sym("->"), recur(node->arrow.right), type);
    case ast_dereference: return triple(alloc, sym("*"), recur(node->dereference.target), type);
    case ast_dereference_assign:
        return quad(alloc, recur(node->dereference_assign.target), sym(".* :="),
                    recur(node->dereference_assign.value), type);

    case ast_assignment:
        return quad(alloc, recur(node->assignment.name), sym("="), recur(node->assignment.value), type);

    case ast_labelled_tuple: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("labelled-tuple"), list, type);

    } break;

    case ast_tuple: {

        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("tuple"), list, type);

    } break;

    case ast_let_in:
        return penta(alloc, sym("let-in"), recur(node->let_in.name), recur(node->let_in.value),
                     recur(node->let_in.body), type);

    case ast_let_match_in:
        return penta(alloc, sym("let-match-in"), recur(node->let_match_in.lt),
                     recur(node->let_match_in.value), recur(node->let_match_in.body), type);

    case ast_let: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return penta(alloc, sym("let"), recur(node->let.name), list, recur(node->let.body), type);

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
        return quad(alloc, sym("function-declaration"), list, recur(node->function_declaration.name), type);

    } break;

    case ast_lambda_declaration: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("lambda-declaration"), list, type);
    }

    case ast_lambda_function_application: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return quad(alloc, sym("lambda-application"), recur(node->lambda_application.lambda), list, type);
    }

    case ast_named_function_application: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return quad(alloc, sym("named-application"), recur(node->named_application.name), list, type);
    }

    case ast_begin_end: {
        sexp list = elements_to_sexp(alloc, node->array.nodes, node->array.n, symbol_fun);
        return triple(alloc, sym("begin-end"), list, type);
    } break;

    case ast_user_type: {
        u16   n        = node->user_type.n_fields;
        sexp *elements = alloc_malloc(alloc, sizeof(sexp) * n);
        for (size_t i = 0; i < n; ++i) elements[i] = recur(node->user_type.fields[i]);
        sexp field_list = sexp_init_list(alloc, elements, n);
        alloc_free(alloc, elements);
        return quad(alloc, sym("user-type"), recur(node->user_type.name), field_list, type);
    }

    case ast_user_type_get: {
        struct ast_user_type_get const *v = ast_node_utg((ast_node *)node);
        return quad(alloc, sym("user-type-get"), recur(v->struct_name), recur(v->field_name), type);
    }

    case ast_user_type_set: {
        struct ast_user_type_set const *v = ast_node_uts((ast_node *)node);
        return penta(alloc, sym("user-type-set"), recur(v->struct_name), recur(v->field_name),
                     recur(v->value), type);
    }

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
    }
    }

#undef pair
#undef triple
#undef quad
#undef penta
#undef recur
#undef sym
#undef symstr
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

    sexp  expr = ast_node_to_sexp(alloc, node);

    int   len  = tl_type_snprint(null, 0, node->type) + 1;
    char *buf  = alloc_malloc(alloc, len);
    tl_type_snprint(buf, len, node->type);

    sexp list = sexp_init_list_pair(alloc, expr, sexp_init_sym_c(alloc, buf));
    alloc_free(alloc, buf);

    return list;
}

// -- pool operations --

void ast_node_each_node(void *ctx, ast_node_each_node_fun fun, ast_node *node) {
    if (!node) return;

    // process node types that have a common ast_array
    if (TL_AST_HAS_ARRAY(node->tag)) {
        struct ast_array *v = ast_node_arr(node);
        for (u32 i = 0; i < v->n; ++i) fun(ctx, v->nodes[i]);
    }

    // process node types that have additional or no-array links
    switch (node->tag) {
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_any:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_tuple:
    case ast_lambda_declaration:
    case ast_begin_end:
    case ast_labelled_tuple:
        //
        return;

    case ast_address_of:
        //
        fun(ctx, node->address_of.target);
        break;

    case ast_arrow:
        fun(ctx, node->arrow.left);
        fun(ctx, node->arrow.right);
        break;

    case ast_assignment:
        fun(ctx, node->assignment.name);
        fun(ctx, node->assignment.value);
        break;

    case ast_dereference:
        //
        fun(ctx, node->dereference.target);
        break;

    case ast_dereference_assign:
        fun(ctx, node->dereference_assign.target);
        fun(ctx, node->dereference_assign.value);
        break;

    case ast_let_in:
        fun(ctx, node->let_in.name);
        fun(ctx, node->let_in.value);
        fun(ctx, node->let_in.body);
        break;

    case ast_let_match_in:
        fun(ctx, node->let_match_in.lt);
        fun(ctx, node->let_match_in.value);
        fun(ctx, node->let_match_in.body);
        break;

    case ast_let:
        //
        fun(ctx, node->let.name);
        fun(ctx, node->let.body);
        break;

    case ast_if_then_else:
        fun(ctx, node->if_then_else.condition);
        fun(ctx, node->if_then_else.yes);
        fun(ctx, node->if_then_else.no);
        break;

    case ast_lambda_function:
        //
        fun(ctx, node->lambda_function.body);
        break;

    case ast_function_declaration:
        //
        fun(ctx, node->function_declaration.name);
        break;

    case ast_lambda_function_application:
        //
        fun(ctx, node->lambda_application.lambda);
        break;

    case ast_named_function_application:
        //
        fun(ctx, node->named_application.name);
        break;

    case ast_user_type:
        //
        fun(ctx, node->user_type.name);
        break;

    case ast_user_type_get:
        fun(ctx, node->user_type_get.struct_name);
        fun(ctx, node->user_type_get.field_name);
        break;

    case ast_user_type_set:
        fun(ctx, node->user_type_set.struct_name);
        fun(ctx, node->user_type_set.field_name);
        fun(ctx, node->user_type_set.value);
        break;
    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);

        fun(ctx, node->user_type_def.name);

        for (u32 i = 0; i < v->n_fields; ++i) {
            fun(ctx, v->field_annotations[i]);
            fun(ctx, v->field_names[i]);
        }

    } break;
    }
}

void ast_node_map_node(void *ctx, ast_node_map_node_fun fun, ast_node *node) {
    if (!node) return;

    // process node types that have a common ast_array
    if (TL_AST_HAS_ARRAY(node->tag)) {
        struct ast_array *v = ast_node_arr(node);
        for (u32 i = 0; i < v->n; ++i) {
            v->nodes[i] = fun(ctx, v->nodes[i]);
        }
    }

    // process node types that have additional or no-array links
    switch (node->tag) {
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_any:
    case ast_bool:
    case ast_symbol:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_tuple:
    case ast_lambda_declaration:
    case ast_begin_end:
    case ast_labelled_tuple:
        //
        return;

    case ast_address_of:
        //
        node->address_of.target = fun(ctx, node->address_of.target);
        break;

    case ast_arrow:
        node->arrow.left  = fun(ctx, node->arrow.left);
        node->arrow.right = fun(ctx, node->arrow.right);
        break;

    case ast_assignment:
        node->assignment.name  = fun(ctx, node->assignment.name);
        node->assignment.value = fun(ctx, node->assignment.value);
        break;

    case ast_dereference:
        //
        node->dereference.target = fun(ctx, node->dereference.target);
        break;

    case ast_dereference_assign:
        node->dereference_assign.target = fun(ctx, node->dereference_assign.target);
        node->dereference_assign.value  = fun(ctx, node->dereference_assign.value);
        break;

    case ast_let_in:
        node->let_in.name  = fun(ctx, node->let_in.name);
        node->let_in.value = fun(ctx, node->let_in.value);
        node->let_in.body  = fun(ctx, node->let_in.body);
        break;

    case ast_let_match_in:
        node->let_match_in.lt    = fun(ctx, node->let_match_in.lt);
        node->let_match_in.value = fun(ctx, node->let_match_in.value);
        node->let_match_in.body  = fun(ctx, node->let_match_in.body);
        break;

    case ast_let:
        //
        node->let.name = fun(ctx, node->let.name);
        node->let.body = fun(ctx, node->let.body);
        break;

    case ast_if_then_else:
        node->if_then_else.condition = fun(ctx, node->if_then_else.condition);
        node->if_then_else.yes       = fun(ctx, node->if_then_else.yes);
        node->if_then_else.no        = fun(ctx, node->if_then_else.no);
        break;

    case ast_lambda_function:
        //
        node->lambda_function.body = fun(ctx, node->lambda_function.body);
        break;

    case ast_function_declaration:
        //
        node->function_declaration.name = fun(ctx, node->function_declaration.name);
        break;

    case ast_lambda_function_application:
        //
        node->lambda_application.lambda = fun(ctx, node->lambda_application.lambda);
        break;

    case ast_named_function_application:
        //
        node->named_application.name = fun(ctx, node->named_application.name);
        break;

    case ast_user_type:
        //
        node->user_type.name = fun(ctx, node->user_type.name);
        break;

    case ast_user_type_get:
        node->user_type_get.struct_name = fun(ctx, node->user_type_get.struct_name);
        node->user_type_get.field_name  = fun(ctx, node->user_type_get.field_name);
        break;

    case ast_user_type_set:
        node->user_type_set.struct_name = fun(ctx, node->user_type_set.struct_name);
        node->user_type_set.field_name  = fun(ctx, node->user_type_set.field_name);
        node->user_type_set.value       = fun(ctx, node->user_type_set.value);
        break;
    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);

        node->user_type_def.name    = fun(ctx, node->user_type_def.name);

        for (u32 i = 0; i < v->n_fields; ++i) {
            v->field_annotations[i] = fun(ctx, v->field_annotations[i]);
            v->field_names[i]       = fun(ctx, v->field_names[i]);
        }

    } break;
    }
}

void ast_node_each_type(void *ctx, ast_node_each_type_fun fun, ast_node *node) {
    // FIXME: unused function

    if (!node) return;

    switch (node->tag) {
    case ast_address_of:
    case ast_arrow:
    case ast_assignment:
    case ast_dereference:
    case ast_dereference_assign:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_any:
    case ast_bool:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_labelled_tuple:
    case ast_tuple:
    case ast_let:
    case ast_let_in:
    case ast_let_match_in:
    case ast_if_then_else:
    case ast_lambda_function:
    case ast_function_declaration:
    case ast_lambda_declaration:
    case ast_lambda_function_application: break;

    case ast_named_function_application:
        if (node->named_application.function_type) fun(ctx, node->named_application.function_type);
        break;

    case ast_begin_end:
    case ast_user_type:
    case ast_user_type_get:
    case ast_user_type_set:
        //
        break;

    case ast_symbol:
        //
        if (node->symbol.annotation_type) fun(ctx, node->symbol.annotation_type);
        break;

        break;

    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);
        for (u32 i = 0; i < v->n_fields; ++i) fun(ctx, v->field_types[i]);
    } break;
    }
}

struct dfs_ctx {
    void      *caller_ctx;
    ast_op_fun fun;
    hashmap   *visited;
};

struct dfs_map_ctx {
    void          *caller_ctx;
    ast_op_map_fun fun;
    hashmap       *visited;
};

void        ast_node_dfs(void *caller_ctx, ast_node *node, ast_op_fun fun);

static void dfs_one(void *ctx_, ast_node *node) {
    struct dfs_ctx *ctx = ctx_;

    if (!node) return;

    // exclude user type defs from dfs
    if (ast_user_type_definition == node->tag) return;

    if (ctx->visited) {
        if (hset_contains(ctx->visited, &node, sizeof(ast_node *))) return;
        hset_insert(&ctx->visited, &node, sizeof(ast_node *));
    }

    ast_node_each_node(ctx, dfs_one, node);
    ctx->fun(ctx->caller_ctx, node);
}

static ast_node *dfs_map_one(void *ctx_, ast_node *node) {
    struct dfs_map_ctx *ctx = ctx_;

    if (!node) return node;

    // exclude user type defs from dfs
    if (ast_user_type_definition == node->tag) return node;

    if (ctx->visited) {
        if (hset_contains(ctx->visited, &node, sizeof(ast_node *))) return node;
        hset_insert(&ctx->visited, &node, sizeof(ast_node *));
    }

    ast_node_map_node(ctx, dfs_map_one, node);
    return ctx->fun(ctx->caller_ctx, node);
}

void ast_node_dfs(void *caller_ctx, ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    if (!node) return;

    struct dfs_ctx ctx = {.caller_ctx = caller_ctx, .fun = fun, .visited = null};
    ast_node_each_node(&ctx, dfs_one, node);
    fun(caller_ctx, node);
}

void ast_node_dfs_safe_for_recur(allocator *alloc, void *caller_ctx, ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    if (!node) return;

    struct dfs_ctx ctx = {
      .caller_ctx = caller_ctx,
      .fun        = fun,
      .visited    = hset_create(alloc, 128),
    };

    ast_node_each_node(&ctx, dfs_one, node);
    fun(caller_ctx, node);

    hset_destroy(&ctx.visited);
}

ast_node *ast_node_map_dfs_safe_for_recur(allocator *alloc, void *caller_ctx, ast_node *node,
                                          ast_op_map_fun fun) {

    // Note: const dfs also uses this function.

    if (!node) return node;

    struct dfs_map_ctx ctx = {
      .caller_ctx = caller_ctx,
      .fun        = fun,
      .visited    = hset_create(alloc, 128),
    };

    ast_node_map_node(&ctx, dfs_map_one, node);
    ast_node *out = fun(caller_ctx, node);

    hset_destroy(&ctx.visited);
    return out;
}

void ast_node_cdfs(void *ctx, ast_node const *start, ast_op_cfun fun) {
    ast_node_dfs(ctx, (ast_node *)start, (ast_op_fun)fun);
}

// -- utilities --

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *ast_tag_to_string(ast_tag tag) {

    static char const *const strings1[] = {
      "ast_nil",
      "ast_address_of",
      "ast_arrow",
      "ast_assignment",
      "ast_bool",
      "ast_dereference",
      "ast_dereference_assign",
      "ast_ellipsis",
      "ast_eof",
      "ast_f64",
      "ast_i64",
      "ast_if_then_else",
      "ast_let_in",
      "ast_let_match_in",
      "ast_string",
      "ast_symbol",
      "ast_u64",
      "ast_user_type_definition",
      "ast_user_type_get",
      "ast_user_type_set",
    };

    static char const *const strings2[] = {
      "ast_begin_end",
      "ast_function_declaration",
      "ast_labelled_tuple",
      "ast_lambda_declaration",
      "ast_lambda_function",
      "ast_lambda_function_application",
      "ast_let",
      "ast_named_function_application",
      "ast_tuple",
      "ast_user_type",
    };

    if (!TL_AST_HAS_ARRAY(tag)) {
        assert(tag < sizeof(strings1));
        return strings1[tag];
    }

    tag = TL_AST_CLEAR_BITS(tag);
    assert(tag < sizeof(strings2));
    return strings2[tag];
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

str_sized ast_nodes_get_names(allocator *alloc, ast_node_slice nodes) {

    str_sized strings = {.v    = alloc_calloc(alloc, nodes.end - nodes.begin, sizeof strings.v[0]),
                         .size = nodes.end - nodes.begin};

    for (u32 i = nodes.begin; i < nodes.end; ++i) strings.v[i - nodes.begin] = ast_node_str(nodes.v[i]);

    return strings;
}

//

struct ast_symbol *ast_node_sym(ast_node *node) {
    assert(node->tag == ast_symbol || node->tag == ast_string);
    return &node->symbol;
}

struct ast_address_of *ast_node_address_of(ast_node *node) {
    assert(node->tag == ast_address_of);
    return &node->address_of;
}

struct ast_arrow *ast_node_arrow(ast_node *node) {
    assert(node->tag == ast_arrow);
    return &node->arrow;
}

struct ast_assignment *ast_node_assignment(ast_node *node) {
    assert(node->tag == ast_assignment);
    return &node->assignment;
}

struct ast_bool *ast_node_bool(ast_node *node) {
    assert(node->tag == ast_bool);
    return &node->bool_;
}

struct ast_dereference *ast_node_deref(ast_node *node) {
    assert(node->tag == ast_dereference);
    return &node->dereference;
}

struct ast_dereference_assign *ast_node_deref_assign(ast_node *node) {
    assert(node->tag == ast_dereference_assign);
    return &node->dereference_assign;
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
    if (!TL_AST_HAS_ARRAY(node->tag)) fatal("ast_node_arr called on non-array variant");
    return &node->array;
}

struct ast_lambda_function *ast_node_lf(ast_node *node) {
    assert(node->tag == ast_lambda_function);
    return &node->lambda_function;
}

struct ast_let_in *ast_node_let_in(ast_node *node) {
    assert(node->tag == ast_let_in);
    return &node->let_in;
}

struct ast_let_match_in *ast_node_let_match_in(ast_node *node) {
    assert(node->tag == ast_let_match_in);
    return &node->let_match_in;
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

struct ast_lambda_application *ast_node_lambda(ast_node *node) {
    assert(node->tag == ast_lambda_function_application);
    return &node->lambda_application;
}

struct ast_named_application *ast_node_named(ast_node *node) {
    assert(node->tag == ast_named_function_application);
    return &node->named_application;
}

struct ast_labelled_tuple *ast_node_lt(ast_node *node) {
    assert(node->tag == ast_labelled_tuple);
    return &node->labelled_tuple;
}

struct ast_tuple *ast_node_tuple(ast_node *node) {
    assert(node->tag == ast_tuple);
    return &node->tuple;
}

struct ast_begin_end *ast_node_begin_end(ast_node *node) {
    assert(node->tag == ast_begin_end);
    return &node->begin_end;
}

struct ast_user_type *ast_node_ut(ast_node *node) {
    assert(node->tag == ast_user_type);
    return &node->user_type;
}

struct ast_user_type_get *ast_node_utg(ast_node *node) {
    assert(node->tag == ast_user_type_get);
    return &node->user_type_get;
}

struct ast_user_type_set *ast_node_uts(ast_node *node) {
    assert(node->tag == ast_user_type_set);
    return &node->user_type_set;
}

struct ast_user_type_def *ast_node_utd(ast_node *node) {
    assert(node->tag == ast_user_type_definition);
    return &node->user_type_def;
}

//

int ast_node_is_specialized(ast_node const *node) {
    return (ast_let == node->tag && BIT_TEST(node->let.flags, AST_LET_FLAG_SPECIALIZED)) ||
           (ast_lambda_function == node->tag &&
            BIT_TEST(node->lambda_function.flags, AST_LAMBDA_FLAG_SPECIALIZED));
}

int ast_node_is_tuple_constructor(ast_node const *node) {
    return (ast_let == node->tag && BIT_TEST(node->let.flags, AST_LET_FLAG_TUPLE_CONS));
}

void ast_node_set_is_specialized(ast_node *node) {
    if (ast_let == node->tag) BIT_SET(node->let.flags, AST_LET_FLAG_SPECIALIZED);
    else if (ast_lambda_function == node->tag)
        BIT_SET(node->lambda_function.flags, AST_LAMBDA_FLAG_SPECIALIZED);
    else if (ast_named_function_application == node->tag)
        BIT_SET(node->named_application.flags, AST_NAMED_APP_SPECIALIZED);
}

void ast_node_set_is_tuple_constructor(ast_node *node) {
    assert(ast_let == node->tag);
    BIT_SET(node->let.flags, AST_LET_FLAG_TUPLE_CONS);
}

tl_type *ast_node_annotation(ast_node const *node) {
    return (ast_symbol == node->tag) ? node->symbol.annotation_type : null;
}

//

ast_node **ast_node_assignment_names(allocator *alloc, ast_node const *node) {
    struct ast_labelled_tuple const *v        = ast_node_lt((ast_node *)node);
    ast_node                       **elements = alloc_malloc(alloc, v->n_assignments * sizeof elements[0]);

    for (u32 i = 0; i < v->n_assignments; ++i) elements[i] = v->assignments[i]->assignment.name;

    return elements;
}

u64 ast_node_hash(ast_node const *self) {
    u64 hash = hash64((byte *)&self->tag, sizeof self->tag);

#define combine_node(node)                                                                                 \
    do {                                                                                                   \
        u64 h = ast_node_hash(node);                                                                       \
        hash  = hash64_combine(hash, (void *)&h, sizeof h);                                                \
    } while (0)

    if (TL_AST_HAS_ARRAY(self->tag))
        for (u32 i = 0; i < self->array.n; ++i) combine_node(self->array.nodes[i]);

    switch (self->tag) {
    case ast_nil:
    case ast_any:
    case ast_ellipsis:
    case ast_eof:      break;

    case ast_address_of:
        //
        combine_node(self->address_of.target);
        break;

    case ast_arrow:
        //
        combine_node(self->arrow.left);
        combine_node(self->arrow.right);
        break;

    case ast_assignment:
        //
        combine_node(self->assignment.name);
        combine_node(self->assignment.value);
        break;

    case ast_bool:
        //
        hash = hash64_combine(hash, (byte *)&self->bool_.val, sizeof(self->bool_.val));
        break;

    case ast_dereference:
        //
        combine_node(self->dereference.target);
        break;

    case ast_dereference_assign:
        combine_node(self->dereference_assign.target);
        combine_node(self->dereference_assign.value);
        break;

    case ast_f64:
        //
        hash = hash64_combine(hash, (byte *)&self->f64.val, sizeof(self->f64.val));
        break;

    case ast_i64:
        //
        hash = hash64_combine(hash, (byte *)&self->i64.val, sizeof(self->i64.val));
        break;

    case ast_if_then_else:
        combine_node(self->if_then_else.condition);
        combine_node(self->if_then_else.yes);
        combine_node(self->if_then_else.no);
        break;

    case ast_let_in:
        combine_node(self->let_in.name);
        combine_node(self->let_in.value);
        combine_node(self->let_in.body);
        break;

    case ast_let_match_in:
        combine_node(self->let_match_in.lt);
        combine_node(self->let_match_in.value);
        combine_node(self->let_match_in.body);
        break;

    case ast_string:
    case ast_symbol: {
        hash = str_hash64_combine(hash, self->symbol.name);
    } break;

    case ast_u64:
        //
        hash = hash64_combine(hash, (byte *)&self->u64.val, sizeof(self->u64.val));
        break;

    case ast_user_type_definition:
        //
        combine_node(self->user_type_def.name);
        break;

    case ast_user_type_get:
        //
        combine_node(self->user_type_get.struct_name);
        combine_node(self->user_type_get.field_name);
        hash = hash64_combine(hash, (byte *)&self->user_type_get.flags, sizeof(self->user_type_get.flags));
        break;

    case ast_user_type_set:
        //
        combine_node(self->user_type_set.struct_name);
        combine_node(self->user_type_set.field_name);
        combine_node(self->user_type_set.value);
        hash = hash64_combine(hash, (byte *)&self->user_type_get.flags, sizeof(self->user_type_get.flags));
        break;

    case ast_begin_end:
        //
        break;

    case ast_function_declaration:
        //
        combine_node(self->function_declaration.name);
        break;

    case ast_labelled_tuple:
        hash = hash64_combine(hash, (byte *)&self->array.flags, sizeof(self->array.flags));
        break;

    case ast_lambda_declaration: break;

    case ast_lambda_function:
        // for lambdas and let functions, its type is part of the hash
        combine_node(self->lambda_function.body);
        // hash = hash64_combine(hash, (byte *)&self->type, sizeof(tl_type *));
        // FIXME
        break;

    case ast_lambda_function_application: combine_node(self->lambda_application.lambda); break;

    case ast_let:
        // for lambdas and let functions, its type is part of the hash
        combine_node(self->let.name);
        combine_node(self->let.body);
        hash = hash64_combine(hash, (byte *)&self->array.flags, sizeof(self->array.flags));
        // hash = hash64_combine(hash, (byte *)&self->type, sizeof(tl_type *));
        // FIXME
        break;

    case ast_named_function_application:
        combine_node(self->named_application.name);
        hash = hash64_combine(hash, (byte *)&self->named_application.function_type, sizeof(tl_type *));
        hash = hash64_combine(hash, (byte *)&self->array.flags, sizeof(self->array.flags));
        break;

    case ast_tuple:
        hash = hash64_combine(hash, (byte *)&self->array.flags, sizeof(self->array.flags));
        break;

    case ast_user_type: combine_node(self->user_type.name); break;
    }

    return hash;
}

//

int ast_node_is_let_in_lambda(ast_node const *self) {
    return (ast_let_in == self->tag && ast_lambda_function == self->let_in.value->tag);
}

int ast_node_is_named_application(ast_node const *self) {
    return (ast_named_function_application == self->tag);
}

tl_type *ast_node_get_arrow(ast_node const *self) {

    if (ast_named_function_application == self->tag) {
        return tl_type_get_arrow(self->named_application.name->type);
    }

    else if (ast_lambda_function_application == self->tag) {
        return tl_type_get_arrow(self->lambda_application.lambda->type);
    }

    else if (ast_address_of == self->tag) {
        return tl_type_get_arrow(self->address_of.target->type);
    }

    else {
        return tl_type_get_arrow(self->type);
    }

    return null;
}

ast_node_sized ast_node_sized_from_ast_array(ast_node *node) {
    return (ast_node_sized){.size = node->array.n, .v = node->array.nodes};
}
