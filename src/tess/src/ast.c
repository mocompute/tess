#include "ast.h"
#include "alloc.h"
#include "ast_tags.h"
#include "hash.h"
#include "hashmap.h"
#include "parser.h"
#include "sexp.h"
#include "str.h"
#include "type.h"

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
    self->error    = tl_err_ok;

    self->tag      = tag;

    return self;
}

ast_node *ast_node_create_bool(allocator *alloc, int val) {
    ast_node *out  = ast_node_create(alloc, ast_bool);
    out->bool_.val = val;
    return out;
}

ast_node *ast_node_create_i64(allocator *alloc, i64 x) {
    ast_node *self = ast_node_create(alloc, ast_i64);
    self->i64.val  = x;
    return self;
}
ast_node *ast_node_create_u64(allocator *alloc, u64 x) {
    ast_node *self = ast_node_create(alloc, ast_u64);
    self->u64.val  = x;
    return self;
}
ast_node *ast_node_create_f64(allocator *alloc, f64 x) {
    ast_node *self = ast_node_create(alloc, ast_f64);
    self->f64.val  = x;
    return self;
}
ast_node *ast_node_create_nfa(allocator *alloc, ast_node *name, ast_node_sized args) {
    ast_node *self                      = ast_node_create(alloc, ast_named_function_application);
    self->named_application.name        = name;
    self->named_application.n_arguments = args.size;
    self->named_application.arguments   = args.v;
    return self;
}
ast_node *ast_node_create_body(allocator *alloc, ast_node_sized body) {
    ast_node *self         = ast_node_create(alloc, ast_body);
    self->body.expressions = body;
    return self;
}
ast_node *ast_node_create_nil(allocator *alloc) {
    ast_node *self = ast_node_create(alloc, ast_nil);
    return self;
}
ast_node *ast_node_create_if_then_else(allocator *alloc, ast_node *cond, ast_node *yes, ast_node *no) {
    ast_node *self               = ast_node_create(alloc, ast_if_then_else);
    self->if_then_else.condition = cond;
    self->if_then_else.yes       = yes;
    self->if_then_else.no        = no;
    return self;
}
ast_node *ast_node_create_unary_op(allocator *alloc, ast_node *op, ast_node *operand) {
    ast_node *self         = ast_node_create(alloc, ast_unary_op);
    self->unary_op.op      = op;
    self->unary_op.operand = operand;
    return self;
}
ast_node *ast_node_create_binary_op(allocator *alloc, ast_node *op, ast_node *left, ast_node *right) {
    ast_node *self        = ast_node_create(alloc, ast_binary_op);
    self->binary_op.op    = op;
    self->binary_op.left  = left;
    self->binary_op.right = right;
    return self;
}
ast_node *ast_node_create_assignment(allocator *alloc, ast_node *name, ast_node *value) {
    ast_node *self         = ast_node_create(alloc, ast_assignment);
    self->assignment.name  = name;
    self->assignment.value = value;
    return self;
}
ast_node *ast_node_create_return(allocator *alloc, ast_node *value, int is_break) {
    ast_node *self                   = ast_node_create(alloc, ast_return);
    self->return_.value              = value;
    self->return_.is_break_statement = is_break;
    return self;
}
ast_node *ast_node_create_continue(allocator *alloc) {
    ast_node *self = ast_node_create(alloc, ast_continue);
    return self;
}
ast_node *ast_node_create_while(allocator *alloc, ast_node *cond, ast_node *body) {
    ast_node *self         = ast_node_create(alloc, ast_while);
    self->while_.condition = cond;
    self->while_.body      = body;
    return self;
}
ast_node *ast_node_create_let_in(allocator *alloc, ast_node *name, ast_node *value, ast_node *body) {
    ast_node *self     = ast_node_create(alloc, ast_let_in);
    self->let_in.name  = name;
    self->let_in.value = value;
    self->let_in.body  = body;
    return self;
}
ast_node *ast_node_create_let(allocator *alloc, ast_node *name, ast_node_sized args, ast_node *body) {
    ast_node *self         = ast_node_create(alloc, ast_let);
    self->let.name         = name;
    self->let.n_parameters = args.size;
    self->let.parameters   = args.v;
    self->let.body         = body;
    return self;
}
ast_node *ast_node_create_tuple(allocator *alloc, ast_node_sized xs) {
    ast_node *self         = ast_node_create(alloc, ast_tuple);
    self->tuple.n_elements = xs.size;
    self->tuple.elements   = xs.v;
    return self;
}
ast_node *ast_node_create_type_alias(allocator *alloc, ast_node *name, ast_node *target) {
    ast_node *self          = ast_node_create(alloc, ast_type_alias);
    self->type_alias.name   = name;
    self->type_alias.target = target;
    return self;
}

ast_node *ast_node_create_arrow(allocator *alloc, ast_node *left, ast_node *right) {
    ast_node *self    = ast_node_create(alloc, ast_arrow);
    self->arrow.left  = left;
    self->arrow.right = right;
    return self;
}

ast_node *ast_node_create_sym_c(allocator *alloc, char const *str) {
    ast_node *self               = ast_node_create(alloc, ast_symbol);
    self->symbol.name            = str_init(alloc, str);
    self->symbol.original        = str_empty();
    self->symbol.annotation      = null;
    self->symbol.annotation_type = null;
    return self;
}

ast_node *ast_node_create_sym(allocator *alloc, str str) {
    ast_node *self               = ast_node_create(alloc, ast_symbol);
    self->symbol.name            = str_copy(alloc, str);
    self->symbol.original        = str_empty();
    self->symbol.annotation      = null;
    self->symbol.annotation_type = null;
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

    if (orig->type) {
        clone->type = tl_polytype_clone(alloc, orig->type);
    } else clone->type = null;

    // clone common array for some tags
    if (TL_AST_HAS_ARRAY(clone->tag)) {
        struct ast_array *vclone = ast_node_arr(clone), *vorig = ast_node_arr((ast_node *)orig);
        vclone->n     = vorig->n;
        vclone->nodes = alloc_malloc(alloc, vorig->n * sizeof vclone->nodes[0]);
        for (u32 i = 0; i < vclone->n; ++i) vclone->nodes[i] = ast_node_clone(alloc, vorig->nodes[i]);
    }

    // clone the rest of the fields
    switch (clone->tag) {
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_tuple:    break;

    case ast_bool:     clone->bool_.val = orig->bool_.val; break;
    case ast_i64:      clone->i64.val = orig->i64.val; break;
    case ast_u64:      clone->u64.val = orig->u64.val; break;
    case ast_f64:      clone->f64.val = orig->f64.val; break;

    case ast_arrow:    {
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

    case ast_symbol:
    case ast_string: {
        struct ast_symbol *vclone = ast_node_sym(clone), *vorig = ast_node_sym((ast_node *)orig);
        vclone->name            = str_copy(alloc, vorig->name);
        vclone->original        = str_copy(alloc, vorig->original);
        vclone->annotation      = ast_node_clone(alloc, vorig->annotation);
        vclone->annotation_type = null;
        if (vorig->annotation_type) {
            vclone->annotation_type = tl_polytype_clone(alloc, vorig->annotation_type);
        }
    } break;

    case ast_hash_command: {
        clone->hash_command.full = str_copy(alloc, orig->hash_command.full);
        str_array arr            = {.alloc = alloc};
        forall(i, orig->hash_command.words) array_push(arr, orig->hash_command.words.v[i]);
        array_shrink(arr);
        clone->hash_command.words      = (str_sized)sized_all(arr);
        clone->hash_command.is_c_block = orig->hash_command.is_c_block;
    } break;

    case ast_let_in: {
        struct ast_let_in *vclone = ast_node_let_in(clone), *vorig = ast_node_let_in((ast_node *)orig);
        vclone->name  = ast_node_clone(alloc, vorig->name);
        vclone->value = ast_node_clone(alloc, vorig->value);
        vclone->body  = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_let: {
        struct ast_let *vclone = ast_node_let(clone), *vorig = ast_node_let((ast_node *)orig);
        vclone->name = ast_node_clone(alloc, vorig->name);
        vclone->body = ast_node_clone(alloc, vorig->body);
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

    case ast_lambda_function_application: {
        struct ast_lambda_application *vclone = ast_node_lambda(clone),
                                      *vorig  = ast_node_lambda((ast_node *)orig);
        vclone->lambda                        = ast_node_clone(alloc, vorig->lambda);
    } break;

    case ast_named_function_application: {
        struct ast_named_application *vclone = ast_node_named(clone),
                                     *vorig  = ast_node_named((ast_node *)orig);
        vclone->name                         = ast_node_clone(alloc, vorig->name);
    } break;

    case ast_type_alias: {
        clone->type_alias.name   = ast_node_clone(alloc, orig->type_alias.name);
        clone->type_alias.target = ast_node_clone(alloc, orig->type_alias.target);
    } break;

    case ast_return:
        //
        clone->return_.value = ast_node_clone(alloc, orig->return_.value);
        break;

    case ast_while:
        clone->while_.condition = ast_node_clone(alloc, orig->while_.condition);
        clone->while_.body      = ast_node_clone(alloc, orig->while_.body);
        break;

    case ast_user_type_definition: {
        struct ast_user_type_def *vclone = ast_node_utd(clone), *vorig = ast_node_utd((ast_node *)orig);

        vclone->name             = ast_node_clone(alloc, vorig->name);
        vclone->n_fields         = vorig->n_fields;
        vclone->n_type_arguments = vorig->n_type_arguments;

        vclone->field_names      = alloc_malloc(alloc, vclone->n_fields * sizeof(ast_node *));

        // enums have no field annotations
        if (vorig->field_annotations)
            vclone->field_annotations = alloc_malloc(alloc, vclone->n_fields * sizeof(ast_node *));
        else vclone->field_annotations = null;

        for (u32 i = 0; i < vclone->n_fields; ++i)
            vclone->field_names[i] = ast_node_clone(alloc, vorig->field_names[i]);

        if (vorig->field_annotations)
            for (u32 i = 0; i < vclone->n_fields; ++i)
                vclone->field_annotations[i] = ast_node_clone(alloc, vorig->field_annotations[i]);

        vclone->type_arguments = null;
        vclone->field_types    = null;

    } break;

    case ast_binary_op: {
        clone->binary_op.left  = ast_node_clone(alloc, orig->binary_op.left);
        clone->binary_op.op    = ast_node_clone(alloc, orig->binary_op.op);
        clone->binary_op.right = ast_node_clone(alloc, orig->binary_op.right);
    } break;

    case ast_body: {
        clone->body.expressions.v = alloc_malloc(alloc, orig->body.expressions.size * sizeof(ast_node *));
        clone->body.expressions.size = orig->body.expressions.size;
        forall(i, clone->body.expressions) {
            clone->body.expressions.v[i] = ast_node_clone(alloc, orig->body.expressions.v[i]);
        }
    } break;

    case ast_unary_op: {
        clone->unary_op.operand = ast_node_clone(alloc, orig->unary_op.operand);
        clone->unary_op.op      = ast_node_clone(alloc, orig->unary_op.op);
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

void ast_node_name_replace(ast_node *node, str replace) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    node->symbol.original = node->symbol.name;
    node->symbol.name     = replace;
}

ast_node *ast_node_lvalue(ast_node *self) {
    if (ast_node_is_symbol(self)) return self;
    else if (ast_binary_op == self->tag) return ast_node_lvalue(self->binary_op.right);
    else if (ast_unary_op == self->tag) return ast_node_lvalue(self->unary_op.operand);
    else fatal("unreachable");
}

ast_node *ast_node_op_rightmost(ast_node *self) {
    if (ast_binary_op == self->tag) return ast_node_op_rightmost(self->binary_op.right);
    else if (ast_unary_op == self->tag) return ast_node_op_rightmost(self->unary_op.operand);
    else return self;
}

void ast_node_type_set(ast_node *self, tl_polytype *type) {
    self->type = type;
}

//

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *));

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *));

sexp symbol_node_to_sexp(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                               sexp_init_sym(alloc, ast_node_str(node)));
}

sexp symbol_node_to_sexp_for_error(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);

    if (!str_is_empty(node->symbol.original))
        return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                                   sexp_init_sym(alloc, node->symbol.original));
    else
        return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                                   sexp_init_sym(alloc, node->symbol.name));
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
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_hash_command:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_tuple:
        //
        return;

    case ast_arrow:
        fun(ctx, node->arrow.left);
        fun(ctx, node->arrow.right);
        break;

    case ast_assignment:
        fun(ctx, node->assignment.name);
        fun(ctx, node->assignment.value);
        break;

    case ast_let_in:
        fun(ctx, node->let_in.name);
        fun(ctx, node->let_in.value);
        fun(ctx, node->let_in.body);
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

    case ast_lambda_function_application:
        //
        fun(ctx, node->lambda_application.lambda);
        break;

    case ast_named_function_application:
        //
        fun(ctx, node->named_application.name);
        break;

    case ast_return:
        //
        fun(ctx, node->return_.value);
        break;

    case ast_while:
        //
        fun(ctx, node->while_.condition);
        fun(ctx, node->while_.body);
        break;

    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);

        fun(ctx, node->user_type_def.name);

        for (u32 i = 0; i < v->n_fields; ++i) {
            if (v->field_annotations) fun(ctx, v->field_annotations[i]);
            fun(ctx, v->field_names[i]);
        }
    } break;

    case ast_type_alias:
        //
        fun(ctx, node->type_alias.name);
        fun(ctx, node->type_alias.target);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            fun(ctx, node->body.expressions.v[i]);
        }
        break;

    case ast_binary_op:
        fun(ctx, node->binary_op.op);
        fun(ctx, node->binary_op.left);
        fun(ctx, node->binary_op.right);
        break;

    case ast_unary_op:
        fun(ctx, node->unary_op.op);
        fun(ctx, node->unary_op.operand);
        break;
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
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_bool:
    case ast_symbol:
    case ast_hash_command:
    case ast_i64:
    case ast_u64:
    case ast_f64:
    case ast_string:
    case ast_tuple:
        //
        return;

    case ast_arrow:
        node->arrow.left  = fun(ctx, node->arrow.left);
        node->arrow.right = fun(ctx, node->arrow.right);
        break;

    case ast_assignment:
        node->assignment.name  = fun(ctx, node->assignment.name);
        node->assignment.value = fun(ctx, node->assignment.value);
        break;

    case ast_let_in:
        node->let_in.name  = fun(ctx, node->let_in.name);
        node->let_in.value = fun(ctx, node->let_in.value);
        node->let_in.body  = fun(ctx, node->let_in.body);
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

    case ast_lambda_function_application:
        //
        node->lambda_application.lambda = fun(ctx, node->lambda_application.lambda);
        break;

    case ast_named_function_application:
        //
        node->named_application.name = fun(ctx, node->named_application.name);
        break;

    case ast_return:
        //
        node->return_.value = fun(ctx, node->return_.value);
        break;

    case ast_while:
        node->while_.condition = fun(ctx, node->while_.condition);
        node->while_.body      = fun(ctx, node->while_.body);
        break;

    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);

        node->user_type_def.name    = fun(ctx, node->user_type_def.name);

        for (u32 i = 0; i < v->n_fields; ++i) {
            v->field_annotations[i] = fun(ctx, v->field_annotations[i]);
            v->field_names[i]       = fun(ctx, v->field_names[i]);
        }

    } break;

    case ast_type_alias:
        //
        node->type_alias.target = fun(ctx, node->type_alias.target);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) {
            fun(ctx, node->body.expressions.v[i]);
        }
        break;

    case ast_binary_op:
        fun(ctx, node->binary_op.op);
        fun(ctx, node->binary_op.left);
        fun(ctx, node->binary_op.right);
        break;

    case ast_unary_op:
        fun(ctx, node->unary_op.op);
        fun(ctx, node->unary_op.operand);
        break;
    }
}

//

ast_arguments_iter ast_node_arguments_iter(ast_node *node) {
    assert(ast_node_is_let(node) || ast_node_is_let_in_lambda(node) || ast_node_is_lambda_function(node) ||
           ast_node_is_lambda_application(node) || ast_node_is_nfa(node));
    if (ast_node_is_let_in_lambda(node)) node = node->let_in.value;

    // These variants all share the same layout for parameters or arguments:
    // ast_let, ast_named_function_application, ast_lambda_application, ast_lambda_function
    return (ast_arguments_iter){
      .index = 0,
      .count = node->array.n ? (node->array.nodes[0]->tag == ast_nil ? 0 : node->array.n) : 0,
      .nodes = (ast_node_sized){.size = node->array.n, .v = node->array.nodes}};
}

ast_node *ast_arguments_next(ast_arguments_iter *iter) {
    if (iter->index >= iter->nodes.size) return null;
    ast_node *node = iter->nodes.v[iter->index++];
    return ast_node_is_nil(node) ? null : node;
}

ast_node *ast_node_body(ast_node *self) {
    if (ast_node_is_let(self)) return self->let.body;
    else if (ast_node_is_let_in_lambda(self)) return self->let_in.value->lambda_function.body;
    else return null;
}

//

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

str v2_ast_node_to_string(allocator *alloc, ast_node const *node) {
    char buf[64];

    str  ty_str = node->type ? tl_polytype_to_string(alloc, node->type) : str_empty();

    switch (node->tag) {
    case ast_hash_command: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("#"));
        str_build_join_sized(&b, S(" "), node->hash_command.words);
        return str_build_finish(&b);
    } break;

    case ast_body: {
        str_build b = str_build_init(alloc, 128);
        forall(i, node->body.expressions) {
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->body.expressions.v[i]));
            str_build_cat(&b, S(" "));
        }
        return str_build_finish(&b);
    } break;

    case ast_binary_op: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->binary_op.left));
        str_build_cat(&b, node->binary_op.op->symbol.name);
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->binary_op.right));
        return str_build_finish(&b);

    } break;

    case ast_f64: snprintf(buf, sizeof buf, "%f", node->f64.val); return str_init(alloc, buf);
    case ast_i64:
        snprintf(buf, sizeof buf, "(%" PRIi64 " : %s)", node->i64.val, str_cstr(&ty_str));
        return str_init(alloc, buf);

    case ast_u64:      snprintf(buf, sizeof buf, "%" PRIu64, node->u64.val); return str_init(alloc, buf);
    case ast_string:   return str_cat_3(alloc, S("\""), node->symbol.name, S("\""));
    case ast_bool:     return node->bool_.val ? str_copy(alloc, S("true")) : str_copy(alloc, S("false"));
    case ast_nil:      return S("()");
    case ast_continue: return S("continue");

    case ast_return:
        //
        return str_cat(alloc, S("return "), v2_ast_node_to_string(alloc, node->return_.value));

    case ast_while:
        //
        return str_cat_4(alloc, S("while ("), v2_ast_node_to_string(alloc, node->while_.condition), S(") "),
                         v2_ast_node_to_string(alloc, node->while_.body));

    case ast_symbol: {
        str out = str_empty();
        if (node->type) out = str_copy(alloc, S("(")); // wrap in () if type exists

        str name     = ast_node_str(node);
        str original = ast_node_name_original(node);
        out          = str_cat(alloc, out, str_is_empty(original) ? name : original);

        if (node->symbol.annotation_type) {
            out = str_cat_4(alloc, out, S(" (ann:"),
                            tl_polytype_to_string(alloc, node->symbol.annotation_type), S(")"));
        } else if (node->symbol.annotation) {
            out =
              str_cat_4(alloc, out, S(" ("), v2_ast_node_to_string(alloc, node->symbol.annotation), S(")"));
        }
        if (node->type) {
            out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type));
            out = str_cat(alloc, out, S(")"));
        }
        return out;
    }

    case ast_let: {
        str out               = str_copy(alloc, S("let "));
        out                   = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let.name));

        ast_node_sized params = ast_node_sized_from_ast_array((ast_node *)node);
        forall(i, params) {
            if (ast_nil == params.v[i]->tag) {
                out = str_cat(alloc, out, S(" ()"));
                break;
            } else {
                out = str_cat_3(alloc, out, S(" "), params.v[i]->symbol.name);
            }
        }

        if (node->type) out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type));
        out = str_cat_3(alloc, out, S(" = "), v2_ast_node_to_string(alloc, node->let.body));
        return out;
    }

    case ast_let_in: {
        str out = str_copy(alloc, S("let "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.name));
        out     = str_cat(alloc, out, S(" = "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.value));
        out     = str_cat(alloc, out, S(" in "));
        if (node->let_in.body) out = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.body));
        return out;

    } break;

    case ast_named_function_application: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(apply "));
        str_build_cat(&b, node->named_application.name->symbol.name);
        for (u32 i = 0, n = node->named_application.n_arguments; i < n; ++i) {
            str_build_cat(&b, S(" "));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->named_application.arguments[i]));
        }
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    } break;

    case ast_arrow: {
        str out = str_cat_3(alloc, v2_ast_node_to_string(alloc, node->arrow.left), S(" -> "),
                            v2_ast_node_to_string(alloc, node->arrow.right));
        return out;
    } break;

    case ast_lambda_function: {
        str out = str_copy(alloc, S("fun"));

        // here we want to include the nil value
        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            out = str_cat_3(alloc, out, S(" "),
                            v2_ast_node_to_string(alloc, node->lambda_function.parameters[i]));
        }
        out = str_cat(alloc, out, S(" -> "));
        out = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->lambda_function.body));
        return out;

    } break;

    case ast_type_alias: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(alias "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_alias.name));
        str_build_cat(&b, S(" "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_alias.target));
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    } break;

    case ast_user_type_definition: {
        str out = str_copy(alloc, S("[user_type_definition: "));
        str_dcat(alloc, &out, node->user_type_def.name->symbol.name);
        str_dcat(alloc, &out, S("]"));
        return out;
    } break;

    case ast_ellipsis:     return str_copy(alloc, S("..."));

    case ast_if_then_else: {
        str_build b = str_build_init(alloc, 80);
        str_build_cat(&b, S("if "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.condition));
        str_build_cat(&b, S(" { "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.yes));
        str_build_cat(&b, S("}"));
        if (node->if_then_else.no) {
            str_build_cat(&b, S(" else { "));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.no));
            str_build_cat(&b, S("}"));
        }
        return str_build_finish(&b);
    }

    break;

    case ast_unary_op:
        return str_cat(alloc, v2_ast_node_to_string(alloc, node->unary_op.op),
                       v2_ast_node_to_string(alloc, node->unary_op.operand));

    case ast_assignment:
        return str_cat_3(alloc, v2_ast_node_to_string(alloc, node->assignment.name), S(" = "),
                         v2_ast_node_to_string(alloc, node->assignment.value));

        break;

    case ast_eof:

    case ast_lambda_function_application:
    case ast_tuple:                       return str_copy(alloc, S("[not implemented]"));
    }
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

struct ast_tuple *ast_node_tuple(ast_node *node) {
    assert(node->tag == ast_tuple);
    return &node->tuple;
}

struct ast_user_type_def *ast_node_utd(ast_node *node) {
    assert(node->tag == ast_user_type_definition);
    return &node->user_type_def;
}

//

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
    case ast_continue:
    case ast_nil:
    case ast_ellipsis:
    case ast_eof:      break;

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

    case ast_string:
    case ast_symbol: {
        hash = str_hash64_combine(hash, self->symbol.name);
    } break;

    case ast_hash_command: hash = str_hash64_combine_sized(hash, self->hash_command.words); break;

    case ast_u64:
        //
        hash = hash64_combine(hash, (byte *)&self->u64.val, sizeof(self->u64.val));
        break;

    case ast_type_alias:
        //
        combine_node(self->type_alias.name);
        combine_node(self->type_alias.target);
        break;

    case ast_user_type_definition:
        //
        combine_node(self->user_type_def.name);
        break;

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
        // hash = hash64_combine(hash, (byte *)&self->type, sizeof(tl_type *));
        // FIXME
        break;

    case ast_named_function_application: combine_node(self->named_application.name); break;

    case ast_body:
        //
        forall(i, self->body.expressions) {
            combine_node(self->body.expressions.v[i]);
        }
        break;

    case ast_binary_op:
        combine_node(self->binary_op.op);
        combine_node(self->binary_op.left);
        combine_node(self->binary_op.right);
        break;

    case ast_unary_op:
        combine_node(self->unary_op.op);
        combine_node(self->unary_op.operand);
        break;

    case ast_return:
        //
        combine_node(self->return_.value);
        break;

    case ast_while:
        combine_node(self->while_.condition);
        combine_node(self->while_.body);
        break;

    case ast_tuple: break;
    }

    return hash;
}

//

int ast_node_is_arrow(ast_node const *self) {
    return ast_arrow == self->tag;
}

int ast_node_is_let_in_lambda(ast_node const *self) {
    return ast_let_in == self->tag && ast_lambda_function == self->let_in.value->tag;
}

int ast_node_is_nfa(ast_node const *self) {
    return ast_named_function_application == self->tag;
}

int ast_node_is_nil(ast_node const *self) {
    return ast_nil == self->tag;
}
int ast_node_is_symbol(ast_node const *self) {
    return ast_symbol == self->tag;
}
int ast_node_is_tuple(ast_node const *self) {
    return ast_tuple == self->tag;
}
int ast_node_is_type_alias(ast_node const *self) {
    return ast_type_alias == self->tag;
}
int ast_node_is_let(ast_node const *self) {
    return ast_let == self->tag;
}
int ast_node_is_let_in(ast_node const *self) {
    return ast_let_in == self->tag;
}
int ast_node_is_utd(ast_node const *self) {
    return ast_user_type_definition == self->tag;
}
int ast_node_is_enum_def(ast_node const *self) {
    return ast_node_is_utd(self) && self->user_type_def.field_annotations == null;
}

int ast_node_is_lambda_function(ast_node const *self) {
    return ast_lambda_function == self->tag;
}
int ast_node_is_lambda_application(ast_node const *self) {
    return ast_lambda_function_application == self->tag;
}
int ast_node_is_assignment(ast_node const *self) {
    return ast_assignment == self->tag;
}
int ast_node_is_binary_op(ast_node const *self) {
    return ast_binary_op == self->tag;
}
int ast_node_is_binary_op_struct_access(ast_node const *self) {
    if (!ast_node_is_binary_op(self)) return 0;
    str op = ast_node_str(self->binary_op.op);
    return is_struct_access_operator(str_cstr(&op));
}
int ast_node_is_body(ast_node const *self) {
    return ast_body == self->tag;
}
int ast_node_is_hash_command(ast_node const *self) {
    return ast_hash_command == self->tag;
}
int ast_node_is_ifc_block(ast_node const *self) {
    return ast_node_is_hash_command(self) && self->hash_command.is_c_block;
}

int ast_node_is_std_application(ast_node const *self) {
    if (!ast_node_is_nfa(self)) return 0;
    return (0 == str_cmp_nc(ast_node_str(self->named_application.name), "std_", 4));
}

ast_node_sized ast_node_sized_from_ast_array(ast_node *node) {
    return (ast_node_sized){.size = node->array.n, .v = node->array.nodes};
}

//

hashmap *ast_node_str_map_create(allocator *alloc, u32 n) {
    return map_create(alloc, sizeof(ast_node *), n);
}

void ast_node_str_map_destroy(hashmap **p) {
    map_destroy(p);
}

void ast_node_str_map_add(hashmap **p, str key, ast_node *val) {
    str_map_set(p, key, &val);
}

void ast_node_str_map_erase(hashmap *map, str key) {
    str_map_erase(map, key);
}

ast_node *ast_node_str_map_get(hashmap *map, str key) {
    ast_node **found = str_map_get(map, key);
    return found ? *found : null;
}

ast_node *ast_node_str_map_iter(hashmap *map, hashmap_iterator *iter) {
    if (map_iter(map, iter)) {
        return *(ast_node **)iter->data;
    }
    return null;
}
