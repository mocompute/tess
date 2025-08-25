#ifndef TESS_AST_POOL_H
#define TESS_AST_POOL_H

#include "alloc.h"
#include "types.h"

// typedef struct ast_node ast_node;

// typedef struct {
//     u32 val;
// } ast_node_h;

// typedef struct ast_pool ast_pool;

// -- allocation and deallocation --

// ast_pool *ast_pool_create(allocator *) mallocfun;
// void      ast_pool_destroy(ast_pool **);

// -- pool operations --
//
// [move_back] takes ownership of ast_node(s) and invalidates caller's copy

nodiscard int   ast_pool_move_back(ast_pool *, ast_node *, ast_node_h *);
ast_node       *ast_pool_at(ast_pool *, ast_node_h);
ast_node const *ast_pool_cat(ast_pool const *, ast_node_h);

#endif
