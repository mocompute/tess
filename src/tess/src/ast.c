#include "ast.h"
#include "alloc.h"
#include "vector.h"

#include <string.h>

// -- statics --

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type_t *ty, type_tag_t tag) {
  memset(ty, 0, sizeof *ty);
  ty->tag = tag;
}

void tess_type_init_type_var(tess_type_t *ty, uint32_t val) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_type_var;
  ty->val = val;
}

void tess_type_init_tuple(tess_type_t *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_tuple;
  mos_vector_init(&ty->tuple, sizeof(ast_node_h));
}

void tess_type_init_arrow(tess_type_t *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_arrow;
}

void tess_type_deinit(mos_allocator_t *alloc, tess_type_t *ty) {
  switch (ty->tag) {
  case type_nil:
  case type_bool:
  case type_int:
  case type_float:
  case type_arrow:
  case type_type_var:
  case type_string:   break;
  case type_tuple:    mos_vector_deinit(alloc, &ty->tuple); break;
  }
  mos_alloc_invalidate(ty, sizeof *ty);
}

// -- tess_type_pool allocation and deallocation --

ast_pool_t *ast_pool_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(ast_pool_t));
}

void ast_pool_dealloc(mos_allocator_t *alloc, ast_pool_t **pool) {
  alloc->free(*pool);
  *pool = 0;
}

int ast_pool_init(mos_allocator_t *alloc, ast_pool_t *pool) {
  mos_vector_init(&pool->data, sizeof(ast_node_t));
  if (mos_vector_reserve(alloc, &pool->data, 32)) return 1;
  return 0;
}

void ast_pool_deinit(mos_allocator_t *alloc, ast_pool_t *pool) {
  // deinit all the ast nodes
  ast_node_t       *it  = mos_vector_begin(&pool->data);
  ast_node_t const *end = mos_vector_end(&pool->data);
  while (it != end) {
    ast_node_deinit(alloc, it++);
  }

  mos_vector_deinit(alloc, &pool->data);
  mos_alloc_invalidate(pool, sizeof *pool);
}

// -- ast_node init and deinit --

void ast_node_deinit(mos_allocator_t *alloc, ast_node_t *node) {

#define deinit(P) mos_vector_deinit(alloc, &P)

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
      alloc->free(node->symbol.name);
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

void ast_node_init(ast_node_t *node, ast_tag_t tag) {

#define init(P) ast_vector_init(&P)

  memset(node, 0, sizeof *node);
  node->tag = tag;

  switch (node->tag) {
  case ast_lambda_function:             init(node->lambda_function.parameters); break;
  case ast_function_declaration:        init(node->function_declaration.parameters); break;
  case ast_lambda_declaration:          init(node->lambda_declaration.parameters); break;
  case ast_let:                         init(node->let.parameters); break;
  case ast_tuple:                       init(node->tuple.elements); break;
  case ast_lambda_function_application: init(node->lambda_function_application.arguments); break;
  case ast_named_function_application:  init(node->named_function_application.arguments); break;

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
  case ast_if_then_else:                break;
  }

#undef init
}

void ast_node_replace(mos_allocator_t *alloc, ast_node_t *node, ast_tag_t tag) {
  ast_node_deinit(alloc, node);
  ast_node_init(node, tag);
}

// -- pool operations --

int ast_pool_move_back(mos_allocator_t *alloc, ast_pool_t *pool, ast_node_t *node, ast_node_h *handle) {

  if (mos_vector_push_back(alloc, &pool->data, node)) return 1;

  handle->val = mos_vector_size(&pool->data) - 1;
  mos_alloc_invalidate(node, sizeof *node);

  return 0;
}

ast_node_t *ast_pool_at(ast_pool_t *pool, ast_node_h handle) {
  return mos_vector_at(&pool->data, handle.val);
}

// -- utilities --

char const *type_tag_to_string(type_tag_t tag) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_TYPE_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  return strings[tag];
}

char const *ast_tag_to_string(ast_tag_t tag) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_AST_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  return strings[tag];
}

int string_to_ast_operator(char const *const s, ast_operator_t *out) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_AST_OPERATOR_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  for (int i = 0; strings[i] != NULL; ++i) {
    if (0 == strcmp(strings[i], s)) {
      *out = (ast_operator_t)i;
      return 0;
    }
  }
  return 1;
}

void ast_vector_init(mos_vector_t *vec) {
  mos_vector_init(vec, sizeof(ast_node_h));
}
