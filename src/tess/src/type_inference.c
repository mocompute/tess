#include "type_inference.h"
#include "alloc.h"
#include "tess_type.h"

struct ti_inferer {
    allocator *ast_alloc;
    ast_node **nodes;
    u32        count;
};

ti_inferer *ti_inferer_create(allocator *alloc, ast_node **nodes, u32 count, allocator *ast_alloc) {
    ti_inferer *self = alloc_calloc(alloc, 1, sizeof *self);
    self->ast_alloc  = ast_alloc;
    self->nodes      = nodes;
    self->count      = count;
    return self;
}

void ti_inferer_destroy(allocator *alloc, ti_inferer **self) {
    alloc_free(alloc, *self);
    *self = null;
}

void ti_inferer_run(ti_inferer *self) {
    ti_assign_type_variables(self->nodes, self->count);

    // TODO ...
}

// -- assign_type_variables --

typedef struct {
    u32 next;
} assign_type_variables_ctx;

void assign_type_variables(void *ctx_, ast_node *node) {
    assign_type_variables_ctx *ctx = ctx_;
    node->type                     = tess_type_init_type_var(ctx->next++);
}

void ti_assign_type_variables(ast_node **nodes, u32 count) {

    assign_type_variables_ctx ctx = {1}; // 0 not valid
    ast_node                **it  = nodes;
    while (count--) ast_pool_dfs(&ctx, *it++, assign_type_variables);
}
