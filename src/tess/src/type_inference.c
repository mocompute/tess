#include "type_inference.h"

#include "alloc.h"
#include "alloc_string.h"
#include "ast_tags.h"
#include "dbg.h"
#include "tess_type.h"
#include "vector.h"

#include <assert.h>

struct ti_inferer {
    allocator *type_arena;
    allocator *strings;
    ast_node **nodes;
    u32        count;

    vectora    constraints;
};

struct constraint {
    struct tess_type const *left;
    struct tess_type const *right;
};

struct solver {
    allocator *alloc;
    vectora    constraints;
    vectora    substitutions;
};

// -- ti_inferer --

static void    ti_assign_type_variables(allocator *, ast_node **, u32);
static vectora ti_collect_constraints(allocator *alloc, ast_node *const *, u32);

ti_inferer    *ti_inferer_create(allocator *alloc, ast_node **nodes, u32 count) {
    ti_inferer *self = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena = alloc_arena_create(alloc, 4096);
    self->strings    = alloc_string_arena_create(alloc, 1024);
    self->nodes      = nodes;
    self->count      = count;
    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    alloc_string_arena_destroy(alloc, &(*self)->strings);
    alloc_arena_destroy(alloc, &(*self)->type_arena);
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_run(ti_inferer *self) {
    ti_assign_type_variables(self->type_arena, self->nodes, self->count);
    self->constraints = ti_collect_constraints(self->type_arena, self->nodes, self->count);

    // TODO ...
}

// -- solver --

void solver_init(allocator *alloc, struct solver *self) {
    self->alloc         = alloc;
    self->constraints   = VECA(alloc, struct constraint);
    self->substitutions = VECA(alloc, struct constraint);
}

void solver_deinit(struct solver *self) {
    veca_deinit(&self->constraints);
    veca_deinit(&self->substitutions);
}

void solver_run(struct solver *self) {
    u32 loop_count = 10;
    while (loop_count--) {

        struct constraint *it  = veca_begin(&self->constraints);
        struct constraint *end = veca_end(&self->constraints);
        while (it != end) {
            // delete a = a constraints
            if (it->left == it->right) {
                veca_erase(&self->constraints, it); // it points to next item
                continue;
            }
        }

        // tuple constraints of equal size
        if (type_tuple == it->left->tag && type_tuple == it->right->tag &&
            vec_size(&it->left->tuple) == vec_size(&it->right->tuple)) {

            // FIXME ...
        }

        bool did_substitute = false;

        if (!did_substitute) break;
    }
}

// -- assign_type_variables --

struct assign_type_variables_ctx {
    allocator *alloc;
    u32        next;
};

void assign_type_variables(void *ctx_, ast_node *node) {
    struct assign_type_variables_ctx *ctx = ctx_;

    if (ast_lambda_function == node->tag || ast_let == node->tag) {
        struct tess_type *left  = alloc_malloc(ctx->alloc, sizeof *left);
        struct tess_type *right = alloc_malloc(ctx->alloc, sizeof *right);
        struct tess_type *arrow = alloc_malloc(ctx->alloc, sizeof *arrow);
        *left                   = tess_type_init_type_var(ctx->next++);
        *right                  = tess_type_init_type_var(ctx->next++);
        *arrow                  = tess_type_init_arrow(left, right);
        node->type              = arrow;
    } else {
        node->type  = alloc_malloc(ctx->alloc, sizeof *node->type);
        *node->type = tess_type_init_type_var(ctx->next++);
    }
}

void ti_assign_type_variables(allocator *alloc, ast_node **nodes, u32 count) {
    struct assign_type_variables_ctx ctx = {alloc, 1}; // 0 not valid
    ast_node                       **it  = nodes;
    while (count--) ast_pool_dfs(&ctx, *it++, assign_type_variables);
}

// -- collect_constraints --

static struct tess_type *arguments_to_tuple_type(allocator *alloc, vector const *arguments) {
    struct tess_type *tuple    = alloc_malloc(alloc, sizeof *tuple);
    *tuple                     = tess_type_init_tuple();

    ast_node const *const *it  = vec_cbegin(arguments);
    ast_node const *const *end = vec_cend(arguments);
    while (it != end) vec_push_back(alloc, &tuple->tuple, (*it++)->type);
    return tuple;
}

static ast_node const *find_let_node(char const *name, ast_node const *const *nodes, u32 count) {
    // TODO profile linear search versus hashmap
    while (count--) {
        ast_node const *const node = *nodes++;
        if (ast_let == node->tag) {
            assert(ast_symbol == node->let.name->tag);
            char const *node_name = mos_string_str(&node->let.name->symbol.name);
            if (0 == strcmp(name, node_name)) return node;
        }
    }
    return null;
}

struct collect_constraints_ctx {
    allocator             *alloc;
    ast_node const *const *nodes;
    u32                    count;
    struct vectora         constraints;
};

void collect_constraints(void *ctx_, ast_node *node) {
    struct collect_constraints_ctx *ctx = ctx_;
    struct constraint               c   = {0};

    switch (node->tag) {
    case ast_eof:
    case ast_nil:
        c = (struct constraint){node->type, tess_type_prim(type_nil)};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_bool:
        c = (struct constraint){node->type, tess_type_prim(type_bool)};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_symbol: break;

    case ast_i64:
    case ast_u64:
        c = (struct constraint){node->type, tess_type_prim(type_int)}; // TODO unsigned
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_f64:
        c = (struct constraint){node->type, tess_type_prim(type_float)};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_string:
        c = (struct constraint){node->type, tess_type_prim(type_string)};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_tuple:
    case ast_function_declaration:
    case ast_lambda_declaration:   break;

    case ast_infix:
        // operands same type
        c = (struct constraint){node->infix.left->type, node->infix.right->type};
        veca_push_back(&ctx->constraints, &c);

        // node same type as operands
        c = (struct constraint){node->type, node->infix.right->type};
        veca_push_back(&ctx->constraints, &c);

        break;

    case ast_let_in:
        // variable name same type as value
        c = (struct constraint){node->let_in.name->type, node->let_in.value->type};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_let:
        // function name same type as node's arrow type
        c = (struct constraint){node->let.name->type, node->type};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_if_then_else:
        // yes and no arms same type, node same type
        c = (struct constraint){node->if_then_else.yes->type, node->if_then_else.no->type};
        veca_push_back(&ctx->constraints, &c);

        c = (struct constraint){node->type, node->if_then_else.yes->type};
        veca_push_back(&ctx->constraints, &c);
        break;

    case ast_lambda_function: {
        // node type is lambda's arrow type, lambda's arrow's left
        // type is same as tuple of parameters, and right is same as
        // function body.
        struct tess_type *tup = arguments_to_tuple_type(ctx->alloc, &node->lambda_function.parameters);
        c                     = (struct constraint){node->type->arrow.left, tup};
        veca_push_back(&ctx->constraints, &c);

        c = (struct constraint){node->type->arrow.right, node->lambda_function.body->type};
        veca_push_back(&ctx->constraints, &c);
        break;

    } break;

    case ast_lambda_function_application: {
        // arguments must match parameters, and node type must match arrow right
        ast_node const *lambda = node->lambda_application.lambda;
        assert(ast_lambda_function == lambda->tag);

        struct tess_type *args   = arguments_to_tuple_type(ctx->alloc, &node->lambda_application.arguments);
        struct tess_type *params = arguments_to_tuple_type(ctx->alloc, &lambda->lambda_function.parameters);
        c                        = (struct constraint){args, params};
        veca_push_back(&ctx->constraints, &c);

        assert(type_arrow == lambda->type->tag);
        c = (struct constraint){node->type, lambda->type->arrow.right};
        veca_push_back(&ctx->constraints, &c);
    } break;

    case ast_named_function_application: {
        // arguments must match parameters, and node type must match arrow right

        assert(ast_symbol == node->named_application.name->tag);
        char const     *name = mos_string_str(&node->named_application.name->symbol.name);
        ast_node const *let  = find_let_node(name, ctx->nodes, ctx->count);
        if (null == let) break;

        struct tess_type *args   = arguments_to_tuple_type(ctx->alloc, &node->named_application.arguments);
        struct tess_type *params = arguments_to_tuple_type(ctx->alloc, &let->let.parameters);
        c                        = (struct constraint){args, params};
        veca_push_back(&ctx->constraints, &c);

        assert(type_arrow == let->type->tag);
        c = (struct constraint){node->type, let->type->arrow.right};
        veca_push_back(&ctx->constraints, &c);
    }

    break;
    }
}

vectora ti_collect_constraints(allocator *alloc, ast_node *const *nodes, u32 count) {
    struct collect_constraints_ctx ctx = {alloc, (ast_node const *const *)nodes, count,
                                          VECA(alloc, struct constraint)};

    ast_node *const               *it  = nodes;
    while (count--) ast_pool_dfs(&ctx, *it++, collect_constraints);
    return ctx.constraints;
}

// typedef void (*vec_map_fun)(void *ctx, void *out, void const *el);

// void vec_map(vector const *, vec_map_fun, void *ctx, void *out);

void dbg_constraint(void *ctx, void *out, void const *el) {
    (void)out;
    char const *left  = tess_type_to_string(ctx, ((struct constraint *)el)->left);
    char const *right = tess_type_to_string(ctx, ((struct constraint *)el)->right);

    dbg("%s = %s\n", left, right);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    veca_map(&self->constraints, dbg_constraint, self->strings, null);
}
