#include "type_inference.h"
#include "alloc.h"
#include "tess_type.h"
#include "vector.h"

#define VEC(T) vec_init_empty(sizeof(T))

struct ti_inferer {
    allocator *type_arena;
    ast_node **nodes;
    u32        count;
};

struct constraint {
    struct tess_type const *left;
    struct tess_type const *right;
};

struct solver {
    allocator *alloc;
    vector     constraints;
    vector     substitutions;
};

// -- ti_inferer --

ti_inferer *ti_inferer_create(allocator *alloc, ast_node **nodes, u32 count) {
    ti_inferer *self = alloc_calloc(alloc, 1, sizeof *self);
    self->type_arena = alloc_arena_create(alloc, 4096);
    self->nodes      = nodes;
    self->count      = count;
    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    alloc_arena_destroy(alloc, &(*self)->type_arena);
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_run(ti_inferer *self) {
    ti_assign_type_variables(self->type_arena, self->nodes, self->count);

    // TODO ...
}

// -- solver --

void solver_init(allocator *alloc, struct solver *self) {
    self->alloc         = alloc;
    self->constraints   = VEC(struct constraint);
    self->substitutions = VEC(struct constraint);
}

void solver_deinit(struct solver *self) {
    vec_deinit(self->alloc, &self->constraints);
    vec_deinit(self->alloc, &self->substitutions);
}

void solver_run(struct solver *self) {
    struct constraint *it  = vec_begin(&self->constraints);
    struct constraint *end = vec_end(&self->constraints);
    while (it != end) {
        // delete a = a constraints
        if (it->left == it->right) {
            vec_erase(&self->constraints, it); // it points to next item
            continue;
        }
    }
}

// -- assign_type_variables --

typedef struct {
    allocator *alloc;
    u32        next;
} assign_type_variables_ctx;

void assign_type_variables(void *ctx_, ast_node *node) {
    assign_type_variables_ctx *ctx = ctx_;
    node->type                     = alloc_malloc(ctx->alloc, sizeof *node->type);
    *node->type                    = tess_type_init_type_var(ctx->next++);
}

void ti_assign_type_variables(allocator *alloc, ast_node **nodes, u32 count) {
    assign_type_variables_ctx ctx = {alloc, 1}; // 0 not valid
    ast_node                **it  = nodes;
    while (count--) ast_pool_dfs(&ctx, *it++, assign_type_variables);
}

// -- constraint --
