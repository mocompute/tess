#include "type_inference.h"

#include "alloc.h"
#include "alloc_string.h"
#include "ast.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "mos_string.h"
#include "tess.h"
#include "tess_type.h"
#include "vector.h"

#include <assert.h>

struct ti_inferer {
    allocator *type_arena;
    allocator *strings;
    vectora   *nodes;

    vectora    constraints;
    vectora    substitutions;
};

struct constraint {
    struct tess_type const *left;
    struct tess_type const *right;
};

struct constraint_iterator {
    struct vector_iterator_base base;
    struct constraint          *ptr;
};

struct solver {
    allocator *alloc;
    allocator *strings;

    // in-out
    vectora *constraints;
    vectora *substitutions;
};

// -- ti_inferer --

static void    ti_assign_type_variables(allocator *, ast_node *[], u32);
static vectora ti_collect_constraints(allocator *alloc, ast_node *[], u32);
static void    ti_apply_substitutions_to_ast(vectora *, ast_node *[], u32);
static void    ti_apply_prim_constraints_to_ast(vectora *, ast_node *[], u32 const);
// static void    ti_specialize_functions(vectora *nodes);

struct solver solver_init(allocator *, vectora *constraints, vectora *substitutions);
void          solver_deinit(struct solver *);
void          solver_run(struct solver *);

ti_inferer   *ti_inferer_create(allocator *alloc, vectora *nodes) {
    ti_inferer *self = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena = alloc_arena_create(alloc, 4096);
    self->strings    = alloc_string_arena_create(alloc, 1024);
    self->nodes      = nodes;
    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    alloc_string_arena_destroy(alloc, &(*self)->strings);
    alloc_arena_destroy(alloc, &(*self)->type_arena);
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_run(ti_inferer *self) {
    ti_assign_type_variables(self->type_arena, veca_data(self->nodes), veca_size(self->nodes));
    self->constraints =
      ti_collect_constraints(self->type_arena, veca_data(self->nodes), veca_size(self->nodes));

    dbg("ti_inferer_run result constraints:\n");
    ti_inferer_dbg_constraints(self);

    self->substitutions  = VECA(self->type_arena, struct constraint);
    struct solver solver = solver_init(self->type_arena, &self->constraints, &self->substitutions);
    solver_run(&solver);
    solver_deinit(&solver);

    ti_apply_substitutions_to_ast(&self->substitutions, veca_data(self->nodes), veca_size(self->nodes));
    ti_apply_prim_constraints_to_ast(&self->constraints, veca_data(self->nodes), veca_size(self->nodes));

    // TODO ...
}

// -- apply substitutions --

void dfs_apply_substitution(void *ctx, ast_node *node) {
    struct constraint *c = ctx;
    if (node->type == c->left) node->type = c->right;
}

static void ti_apply_substitutions_to_ast(vectora *substitutions, ast_node *nodes[], u32 const count) {

    struct constraint_iterator iter = {0};
    while (veca_iter(substitutions, &iter.base)) {
        for (size_t i = 0; i < count; ++i) ast_pool_dfs(iter.ptr, nodes[i], dfs_apply_substitution);
    }
}

void dfs_apply_prim_constraints(void *ctx, ast_node *node) {
    struct constraint *c = ctx;
    if (node->type == c->left && tess_type_is_prim(c->right)) node->type = c->right;
}

static void ti_apply_prim_constraints_to_ast(vectora *constraints, ast_node *nodes[], u32 const count) {

    struct constraint_iterator iter = {0};
    while (veca_iter(constraints, &iter.base)) {
        for (size_t i = 0; i < count; ++i) ast_pool_dfs(iter.ptr, nodes[i], dfs_apply_prim_constraints);
    }
}

// -- solver --

static void dbg_constraint(struct constraint const *c) {

    int  len_left  = tess_type_snprint(null, 0, c->left) + 1;
    int  len_right = tess_type_snprint(null, 0, c->right) + 1;
    char buf_left[len_left], buf_right[len_right];
    tess_type_snprint(buf_left, len_left, c->left);
    tess_type_snprint(buf_right, len_right, c->right);
    dbg("constraint %p: %s (%p) = %s (%p)\n", c, buf_left, c->left, buf_right, c->right);
}

struct solver solver_init(allocator *alloc, vectora *constraints, vectora *substitutions) {
    struct solver self;
    self.alloc         = alloc;
    self.strings       = alloc_string_arena_create(alloc, 1024);
    self.constraints   = constraints;
    self.substitutions = substitutions;
    return self;
}

void solver_deinit(struct solver *self) {
    alloc_string_arena_destroy(self->alloc, &self->strings);
}

static bool substitute_constraints(struct constraint *begin, struct constraint *end,
                                   struct constraint const sub) {

    bool did_substitute = false;

    // dbg("substitute_constraint: %p -> %p\n", sub.left, sub.right);

    // this uses reference identity: different pointers are considered
    // unequal even if they are structurally equal.
    while (begin != end) {
        if (begin->left == sub.left) {
            begin->left    = sub.right;
            did_substitute = true;
        }
        if (begin->right == sub.left) {
            begin->right   = sub.right;
            did_substitute = true;
        }
        ++begin;
    }

    return did_substitute;
}

static bool unify_one(struct solver *self, struct constraint c) {

    if (c.left == c.right) return false;

    // typevar1 = typevar2 : replace all tv1s
    else if (type_type_var == c.left->tag && type_type_var == c.right->tag) {
        // dbg("adding substitution: ");
        // dbg_constraint(iter.ptr);

        struct constraint_iterator iter = {.ptr = &c};
        veca_iterator_init(self->substitutions, &iter.base);
        veca_push_back(self->substitutions, &iter.base);
    }

    // tv1 = tv2 -> tv3 : replace tv1s with the arrow type
    else if (type_type_var == c.left->tag && type_arrow == c.right->tag &&
             type_type_var == c.right->arrow.left->tag && type_type_var == c.right->arrow.right->tag) {

        // dbg("adding substitution: ");
        // dbg_constraint(iter.ptr);

        struct constraint_iterator iter = {.ptr = &c};
        veca_iterator_init(self->substitutions, &iter.base);
        veca_push_back(self->substitutions, &iter.base);
    }

    // tuple constraints of equal size
    else if (type_tuple == c.left->tag && type_tuple == c.right->tag &&
             vec_size(&c.left->tuple) == vec_size(&c.right->tuple)) {

        struct tess_type_citerator left  = {0};
        struct tess_type_citerator right = {0};
        while (vec_citer(&c.left->tuple, &left.base)) {
            if (!vec_citer(&c.right->tuple, &right.base)) fatal("solver_run: vector size mismatch");

            unify_one(self, (struct constraint){*left.ptr, *right.ptr});
        }
    }

    // arrow types
    else if (type_arrow == c.left->tag && type_arrow == c.right->tag) {}

    return true;
}

void solver_run(struct solver *self) {
    u32 loop_count = 10;
    while (loop_count--) {

        struct constraint_iterator iter = {0};
        while (veca_iter(self->constraints, &iter.base)) {

            // dbg_constraint(iter.ptr);

            // delete a = a constraints
            if (iter.ptr->left == iter.ptr->right) {

                veca_erase(self->constraints, &iter.base);
                continue;

            } else {

                if (unify_one(self, *iter.ptr)) {
                    // iterate through remainder of constraints and substitute
                    substitute_constraints(&iter.ptr[1], veca_end(self->constraints), *iter.ptr);
                }
            }
        }

        bool did_substitute = false;

        // apply each substitution in sequence to constraints
        iter = (struct constraint_iterator){0};
        while (veca_iter(self->substitutions, &iter.base)) {
            if (substitute_constraints(veca_begin(self->constraints), veca_end(self->constraints),
                                       *iter.ptr))
                did_substitute = true;
        }

        // apply each substitution to all known ast nodes

        if (!did_substitute) break;
    }
}

// -- assign_type_variables --

struct assign_type_variables_ctx {
    allocator *alloc;
    hashmap   *symbols;
    u32        next;
};

void assign_type_variables(void *ctx_, ast_node *node) {
    struct assign_type_variables_ctx *ctx = ctx_;

    // ensure symbols with the same name are assigned the same type
    // variable

    if (ast_symbol == node->tag) {

        struct tess_type **found = map_get(ctx->symbols, mos_string_str(&node->symbol.name),
                                           (u16)mos_string_size(&node->symbol.name));
        if (found) {
            node->type = *found;
            dbg("copied %u to %s\n", (*found)->type_var, mos_string_str(&node->symbol.name));
            assert(node->type->tag != type_nil);
        } else {
            struct tess_type *assign = tess_type_create_type_var(ctx->alloc, ctx->next++);
            node->type               = assign;
            map_set(&ctx->symbols, mos_string_str(&node->symbol.name),
                    (u16)mos_string_size(&node->symbol.name), &assign);

            dbg("assigned %u to %s\n", ctx->next - 1, mos_string_str(&node->symbol.name));
            assert(node->type->tag != type_nil);
        }
    }

    else if (ast_lambda_function == node->tag || ast_let == node->tag) {
        struct tess_type *left  = tess_type_create_type_var(ctx->alloc, ctx->next++);
        struct tess_type *right = tess_type_create_type_var(ctx->alloc, ctx->next++);
        struct tess_type *arrow = tess_type_create_arrow(ctx->alloc, left, right);
        node->type              = arrow;
        dbg("assign_type_variables: name = %p\n", node->let.name);
    } else {
        struct tess_type *tv = tess_type_create_type_var(ctx->alloc, ctx->next++);
        node->type           = tv;
    }
}

void ti_assign_type_variables(allocator *alloc, ast_node *nodes[], u32 count) {
    struct assign_type_variables_ctx ctx = {
      .alloc   = alloc,
      .symbols = map_create(alloc, sizeof(struct tess_type *)),
      .next    = 1, // 0 not valid
    };

    for (size_t i = 0; i < count; ++i) {
        ast_pool_dfs(&ctx, nodes[i], assign_type_variables);
    }

    map_destroy(&ctx.symbols);
}

// -- collect_constraints --

static struct tess_type *arguments_to_tuple_type(allocator *alloc, vector const *arguments) {
    struct tess_type          *tuple     = tess_type_create_tuple(alloc);

    struct ast_node_iterator   iter      = {0};
    struct tess_type_citerator type_iter = {0};
    vec_iterator_init(arguments, &iter.base);
    vec_iterator_init(&tuple->tuple, &type_iter.base);

    while (vec_citer(arguments, &iter.base)) {
        type_iter.ptr = &(*iter.ptr)->type;
        vec_push_back(alloc, &tuple->tuple, &type_iter.base);
    }

    return tuple;
}

static ast_node const *find_let_node(char const *name, ast_node *nodes[], u32 count) {
    // TODO profile linear search versus hashmap

    for (size_t i = 0; i < count; ++i) {
        if (ast_let == nodes[i]->tag) {
            assert(ast_symbol == nodes[i]->let.name->tag);
            char const *node_name = mos_string_str(&nodes[i]->let.name->symbol.name);
            if (0 == strcmp(name, node_name)) return nodes[i];
        }
    }
    return null;
}

struct collect_constraints_ctx {
    allocator     *alloc;
    ast_node     **nodes;
    u32            count;
    struct vectora constraints;
};

void collect_constraints(void *ctx_, ast_node *node) {
    struct collect_constraints_ctx *ctx = ctx_;
    struct constraint               c   = {0};

#define push(L, R)                                                                                         \
    do {                                                                                                   \
        c                               = (struct constraint){(L), (R)};                                   \
        struct constraint_iterator iter = {.ptr = &c};                                                     \
        veca_iterator_init(&ctx->constraints, &iter.base);                                                 \
        veca_push_back(&ctx->constraints, &iter.base);                                                     \
    } while (0)

    switch (node->tag) {
    case ast_eof:
    case ast_nil:    push(node->type, tess_type_prim(type_nil)); break;
    case ast_bool:   push(node->type, tess_type_prim(type_bool)); break;

    case ast_symbol: break;

    case ast_i64:
    case ast_u64:
        push(node->type, tess_type_prim(type_int)); // TODO unsigned
        break;

    case ast_f64:    push(node->type, tess_type_prim(type_float)); break;
    case ast_string: push(node->type, tess_type_prim(type_string)); break;

    case ast_tuple:  {
        struct tess_type *els = arguments_to_tuple_type(ctx->alloc, &node->tuple.elements);
        push(node->type, els);
    } break;

    case ast_function_declaration:
    case ast_lambda_declaration:
        /* function_declaration and lambda_declaration only appear during compilation */
        break;

    case ast_infix:
        // operands must be same type
        push(node->infix.left->type, node->infix.right->type);

        // result must be same type
        push(node->type, node->infix.right->type);

        break;

    case ast_let_in:
        // variable name same type as value
        push(node->let_in.name->type, node->let_in.value->type);

        // result must be same type as value
        push(node->type, node->let_in.value->type);
        break;

    case ast_let:
        // function name same type as node's arrow type
        push(node->let.name->type, node->type);

        // result is nil
        push(node->type, tess_type_prim(type_nil));
        break;

    case ast_if_then_else:
        // yes and no arms same type
        push(node->if_then_else.yes->type, node->if_then_else.no->type);

        // result is same type as arms
        push(node->type, node->if_then_else.yes->type);
        break;

    case ast_lambda_function: {
        // argument tuple must be same type as parameter tuple
        assert(type_arrow == node->type->tag);

        struct tess_type *tup = arguments_to_tuple_type(ctx->alloc, &node->lambda_function.parameters);
        push(node->type->arrow.left, tup);

        // body type must be same as right hand of arrow
        push(node->type->arrow.right, node->lambda_function.body->type);

        // result must be same as right hand of arrow
        push(node->type, node->type->arrow.right);
        break;

    } break;

    case ast_lambda_function_application: {
        // lambda must be a ast_lambda_functoin
        ast_node const *lambda = node->lambda_application.lambda;
        assert(ast_lambda_function == lambda->tag);

        // arguments must match parameters
        struct tess_type *args   = arguments_to_tuple_type(ctx->alloc, &node->lambda_application.arguments);
        struct tess_type *params = arguments_to_tuple_type(ctx->alloc, &lambda->lambda_function.parameters);
        push(args, params);

        // result must be same as right hand of arrow
        assert(type_arrow == lambda->type->tag);
        push(node->type, lambda->type->arrow.right);
    } break;

    case ast_named_function_application: {
        // must find function definition in a prior ast_let node. Look
        // for matching symbol name.
        assert(ast_symbol == node->named_application.name->tag);
        char const     *name = mos_string_str(&node->named_application.name->symbol.name);
        ast_node const *let  = find_let_node(name, ctx->nodes, ctx->count);
        if (null == let) fatal("collect_constraints: can't find let node for function application.");

        // arguments must match parameters
        struct tess_type *args   = arguments_to_tuple_type(ctx->alloc, &node->named_application.arguments);
        struct tess_type *params = arguments_to_tuple_type(ctx->alloc, &let->let.parameters);
        push(args, params);

        // result must be same as right hand of arrow
        assert(type_arrow == let->type->tag);
        push(node->type, let->type->arrow.right);
    }

    break;
    }

#undef push
}

vectora ti_collect_constraints(allocator *alloc, ast_node *nodes[], u32 count) {
    struct collect_constraints_ctx ctx = {alloc, nodes, count, VECA(alloc, struct constraint)};

    for (size_t i = 0; i < count; ++i) ast_pool_dfs(&ctx, nodes[i], collect_constraints);
    return ctx.constraints;
}

static void map_dbg_constraint(void *ctx, void *out, void const *el) {
    (void)ctx;
    (void)out;
    dbg_constraint(el);
}

void ti_inferer_dbg_constraints(ti_inferer const *self) {
    veca_map(&self->constraints, map_dbg_constraint, null, null);
}

void ti_inferer_dbg_substitutions(ti_inferer const *self) {
    dbg("substitutions count = %u\n", veca_size(&self->substitutions));
    veca_map(&self->substitutions, map_dbg_constraint, null, null);
}

// -- specialize function applications --

#if 0
struct specialize_functions_ctx {
    vectora *nodes;
};

static void specialize_node(void *ctx_, ast_node *node) {
    struct specialize_functions_ctx *ctx = ctx_;

    if (node->tag != ast_named_function_application) return;
    assert(ast_symbol == node->named_application.name->tag);

    ast_node const *let = find_let_node(mos_string_str(&node->named_application.name->symbol.name),
                                        veca_data(ctx->nodes), veca_size(ctx->nodes));

    // TODO compiler error
    if (null == let) fatal("specialize_node: can't find let node for function application.");
}

static void ti_specialize_functions(vectora *nodes) {
    struct specialize_functions_ctx ctx  = {nodes};
    struct ast_node_iterator        iter = {0};
    while (vec_iter((vector *)nodes, &iter.base)) ast_pool_dfs(&ctx, *iter.ptr, specialize_node);
}
#endif
