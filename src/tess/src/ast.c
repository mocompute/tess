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
  ty->tag = tess_ty_type_var;
  ty->val = val;
}

void tess_type_init_tuple(tess_type_t *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = tess_ty_tuple;
  mos_vector_init(&ty->tuple, sizeof(size_t));
}

void tess_type_init_arrow(tess_type_t *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = tess_ty_arrow;
}

void tess_type_deinit(mos_allocator_t *alloc, tess_type_t *ty) {
  switch (ty->tag) {
  case tess_ty_nil:
  case tess_ty_bool:
  case tess_ty_int:
  case tess_ty_float:
  case tess_ty_arrow:
  case tess_ty_type_var:
  case tess_ty_string:   break;
  case tess_ty_tuple:    mos_vector_deinit(alloc, &ty->tuple); break;
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

void ast_pool_init(mos_allocator_t *alloc, ast_pool_t *pool) {
  mos_vector_init(&pool->data, sizeof(ast_node_t));
  mos_vector_reserve(alloc, &pool->data, 32);
}

void ast_pool_deinit(mos_allocator_t *alloc, ast_pool_t *pool) {
  mos_vector_deinit(alloc, &pool->data);
  mos_alloc_invalidate(pool, sizeof *pool);
}

// -- tess_ast_node init and deinit --

void ast_node_deinit(mos_allocator_t *alloc, ast_node_t *node) {

#define deinit(P) mos_vector_deinit(alloc, &P)

  switch (node->tag) {
  case tess_ast_lambda_function:             deinit(node->lambda_function.parameters); break;
  case tess_ast_function_declaration:        deinit(node->function_declaration.parameters); break;
  case tess_ast_lambda_declaration:          deinit(node->lambda_declaration.parameters); break;
  case tess_ast_let:                         deinit(node->let.parameters); break;
  case tess_ast_tuple:                       deinit(node->tuple.elements); break;
  case tess_ast_lambda_function_application: deinit(node->lambda_function_application.arguments); break;
  case tess_ast_named_function_application:  deinit(node->named_function_application.arguments); break;
  case tess_ast_eof:
  case tess_ast_nil:
  case tess_ast_bool:
  case tess_ast_symbol:
  case tess_ast_i64:
  case tess_ast_u64:
  case tess_ast_f64:
  case tess_ast_string:
  case tess_ast_infix:
  case tess_ast_let_in:
  case tess_ast_if_then_else:                break;
  }

  // TODO: intern or pool symbol strings
  if (node->name) {
    alloc->free(node->name);
    node->name = 0;
  }

#undef deinit
}

void ast_node_init(ast_node_t *node, ast_tag_t tag) {

#define init(P) mos_vector_init(&P, sizeof(size_t))

  memset(node, 0, sizeof *node);
  node->tag = tag;

  switch (node->tag) {
  case tess_ast_lambda_function:             init(node->lambda_function.parameters); break;
  case tess_ast_function_declaration:        init(node->function_declaration.parameters); break;
  case tess_ast_lambda_declaration:          init(node->lambda_declaration.parameters); break;
  case tess_ast_let:                         init(node->let.parameters); break;
  case tess_ast_tuple:                       init(node->tuple.elements); break;
  case tess_ast_lambda_function_application: init(node->lambda_function_application.arguments); break;
  case tess_ast_named_function_application:  init(node->named_function_application.arguments); break;

  case tess_ast_eof:
  case tess_ast_nil:
  case tess_ast_bool:
  case tess_ast_symbol:
  case tess_ast_i64:
  case tess_ast_u64:
  case tess_ast_f64:
  case tess_ast_string:
  case tess_ast_infix:
  case tess_ast_let_in:
  case tess_ast_if_then_else:                break;
  }

#undef init
}

// -- pool operations --

int ast_pool_move_back(mos_allocator_t *alloc, ast_pool_t *pool, ast_node_t *node, size_t *handle) {

  if (mos_vector_push_back(alloc, &pool->data, node)) return 1;

  *handle = mos_vector_size(&pool->data) - 1;
  mos_alloc_invalidate(node, sizeof *node);

  return 0;
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
