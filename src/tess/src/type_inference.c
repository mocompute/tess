#include "type_inference.h"

#include "alloc.h"
#include "alloc_string.h"
#include "ast_tags.h"
#include "dbg.h"
#include "hashmap.h"
#include "mos_string.h"
#include "tess_type.h"
#include "vector.h"

#include <assert.h>

struct ti_inferer {
    allocator *type_arena;
    allocator *strings;
    ast_node **nodes;
    u32        count;

    vectora    constraints;
    vectora    substitutions;
};

struct constraint {
    struct tess_type const *left;
    struct tess_type const *right;
};

struct solver {
    allocator *alloc;
    allocator *strings;

    // in-out
    vectora *constraints;
    vectora *substitutions;
};

// -- ti_inferer --

static void    ti_assign_type_variables(allocator *, ast_node **, u32);
static vectora ti_collect_constraints(allocator *alloc, ast_node *const *, u32);

struct solver  solver_init(allocator *, vectora *constraints, vectora *substitutions);
void           solver_deinit(struct solver *);
void           solver_run(struct solver *);

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

    dbg("ti_inferer_run result constraints:\n");
    ti_inferer_dbg_constraints(self);

    self->substitutions  = VECA(self->type_arena, struct constraint);
    struct solver solver = solver_init(self->type_arena, &self->constraints, &self->substitutions);
    solver_run(&solver);

    solver_deinit(&solver);

    // TODO ...
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

    dbg("substitute_constraint: %p -> %p\n", sub.left, sub.right);

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

void solver_run(struct solver *self) {
    u32 loop_count = 10;
    while (loop_count--) {

        struct constraint *it = veca_begin(self->constraints);
        while (it != veca_end(self->constraints)) {

            dbg_constraint(it);

            // delete a = a constraints
            if (it->left == it->right) {
                dbg("deleting constraint %p\n", it);
                veca_erase(self->constraints, it); // it points to next item, but end will change
                continue;                          // skip iterator increment at bottom of loop
            }

            // typevar1 = typevar2 : replace all tv1s
            else if (type_type_var == it->left->tag && type_type_var == it->right->tag) {
                dbg("adding substitution: ");
                dbg_constraint(it);
                veca_push_back(self->substitutions, it);

                // iterate through remainder of constraints and substitute
                substitute_constraints(&it[1], veca_end(self->constraints), *it);
            }

            // tuple constraints of equal size
            else if (type_tuple == it->left->tag && type_tuple == it->right->tag &&
                     vec_size(&it->left->tuple) == vec_size(&it->right->tuple)) {

                // FIXME ...
            }

            ++it;
        }

        bool did_substitute = false;

        // apply each substitution in sequence to constraints
        it = veca_begin(self->substitutions);
        while (it != veca_end(self->substitutions)) {
            if (substitute_constraints(veca_begin(self->constraints), veca_end(self->constraints), *it))
                did_substitute = true;
            ++it;
        }

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

        u32                h     = mos_string_hash32(&node->symbol.name);
        struct tess_type **found = map_get(ctx->symbols, h);
        if (found) {
            node->type = *found;
            dbg("copied %u to %s\n", (*found)->type_var, mos_string_str(&node->symbol.name));
            assert(node->type->tag != type_nil);
        } else {
            struct tess_type *assign = alloc_struct(ctx->alloc, assign);
            *assign                  = tess_type_init_type_var(ctx->next++);

            node->type               = assign;
            if (map_set(ctx->alloc, &ctx->symbols, h, &assign)) {
                dbg("map_set failed.\n");
                assert(false);
                return;
            }

            dbg("assigned %u to %s\n", ctx->next - 1, mos_string_str(&node->symbol.name));
            assert(node->type->tag != type_nil);
        }
    }

    else if (ast_lambda_function == node->tag || ast_let == node->tag) {
        struct tess_type *left  = alloc_struct(ctx->alloc, left);
        struct tess_type *right = alloc_struct(ctx->alloc, right);
        struct tess_type *arrow = alloc_struct(ctx->alloc, arrow);
        *left                   = tess_type_init_type_var(ctx->next++);
        *right                  = tess_type_init_type_var(ctx->next++);
        *arrow                  = tess_type_init_arrow(left, right);
        node->type              = arrow;
    } else {
        node->type  = alloc_struct(ctx->alloc, node->type);
        *node->type = tess_type_init_type_var(ctx->next++);
    }
}

void ti_assign_type_variables(allocator *alloc, ast_node **nodes, u32 count) {
    struct assign_type_variables_ctx ctx = {
      .alloc   = alloc,
      .symbols = map_create(alloc, sizeof(struct tess_type *), 256, 0),
      .next    = 1, // 0 not valid
    };

    ast_node **it = nodes;
    while (count--) ast_pool_dfs(&ctx, *it++, assign_type_variables);

    map_destroy(alloc, &ctx.symbols);
}

// -- collect_constraints --

static struct tess_type *arguments_to_tuple_type(allocator *alloc, vector const *arguments) {
    struct tess_type *tuple    = alloc_struct(alloc, tuple);
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
