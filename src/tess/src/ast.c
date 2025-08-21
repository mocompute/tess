#include "ast.h"
#include "alloc.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

// -- statics --

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type *ty, type_tag tag) {
  memset(ty, 0, sizeof *ty);
  ty->tag = tag;
}

void tess_type_init_type_var(tess_type *ty, uint32_t val) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_type_var;
  ty->val = val;
}

int tess_type_init_tuple(mos_allocator *alloc, tess_type *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_tuple;
  return mos_vector_init(alloc, &ty->tuple, sizeof(ast_node_h), 0);
}

void tess_type_init_arrow(tess_type *ty) {
  memset(ty, 0, sizeof *ty);
  ty->tag = type_arrow;
}

void tess_type_deinit(mos_allocator *alloc, tess_type *ty) {
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

ast_pool *ast_pool_alloc(mos_allocator *alloc) {
  return alloc->malloc(alloc, sizeof(ast_pool));
}

ast_pool *ast_pool_alloci(mos_allocator *alloc) {
  ast_pool *out = alloc->malloc(alloc, sizeof(ast_pool));
  if (!out) return out;

  if (ast_pool_init(alloc, out)) {
    alloc->free(alloc, out);
    return NULL;
  }
  return out;
}

void ast_pool_dealloc(mos_allocator *alloc, ast_pool **pool) {
  mos_alloc_assert_invalid(*pool, sizeof *pool);
  alloc->free(alloc, *pool);
  *pool = NULL;
}

void ast_pool_dealloci(mos_allocator *alloc, ast_pool **pool) {
  ast_pool_deinit(alloc, *pool);
  ast_pool_dealloc(alloc, pool);
}

int ast_pool_init(mos_allocator *alloc, ast_pool *pool) {
  return mos_vector_init(alloc, &pool->data, sizeof(ast_node), 32);
}

void ast_pool_deinit(mos_allocator *alloc, ast_pool *pool) {
  // deinit all the ast nodes
  ast_node       *it  = mos_vector_begin(&pool->data);
  ast_node const *end = mos_vector_end(&pool->data);
  while (it != end) {
    ast_node_deinit(alloc, it++);
  }

  mos_vector_deinit(alloc, &pool->data);
  mos_alloc_invalidate(pool, sizeof *pool);
}

// -- ast_node init and deinit --

void ast_node_deinit(mos_allocator *alloc, ast_node *node) {

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
      alloc->free(alloc, node->symbol.name);
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

int ast_node_init(mos_allocator *alloc, ast_node *node, ast_tag tag) {

  // accepts alloc = NULL in some cases

#define init(P)                                                                                            \
  do {                                                                                                     \
    assert(alloc);                                                                                         \
    return ast_vector_init(alloc, &P);                                                                     \
  } while (0)

  memset(node, 0, sizeof *node);
  node->tag = tag;

  switch (node->tag) {
  case ast_lambda_function:             init(node->lambda_function.parameters);
  case ast_function_declaration:        init(node->function_declaration.parameters);
  case ast_lambda_declaration:          init(node->lambda_declaration.parameters);
  case ast_let:                         init(node->let.parameters);
  case ast_tuple:                       init(node->tuple.elements);
  case ast_lambda_function_application: init(node->lambda_function_application.arguments);
  case ast_named_function_application:  init(node->named_function_application.arguments);

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
  case ast_if_then_else:                return 0;
  }

#undef init
}

int ast_node_replace(mos_allocator *alloc, ast_node *node, ast_tag tag) {
  ast_node_deinit(alloc, node);
  return ast_node_init(alloc, node, tag);
}

// -- pool operations --

int ast_pool_move_back(mos_allocator *alloc, ast_pool *pool, ast_node *node, ast_node_h *handle) {

  if (mos_vector_push_back(alloc, &pool->data, node)) return 1;

  handle->val = mos_vector_size(&pool->data) - 1;
  mos_alloc_invalidate(node, sizeof *node);

  return 0;
}

ast_node *ast_pool_at(ast_pool *pool, ast_node_h handle) {
  return mos_vector_at(&pool->data, handle.val);
}

// -- utilities --

char const *type_tag_to_string(type_tag tag) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_TYPE_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  return strings[tag];
}

char const *ast_tag_to_string(ast_tag tag) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_AST_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  return strings[tag];
}

char const *ast_operator_to_string(ast_operator tag) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_AST_OPERATOR_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  return strings[tag];
}

int string_to_ast_operator(char const *const s, ast_operator *out) {

#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_AST_OPERATOR_TAGS(STRING_ITEM)};
#undef STRING_ITEM

  for (int i = 0; strings[i] != NULL; ++i) {
    if (0 == strcmp(strings[i], s)) {
      *out = (ast_operator)i;
      return 0;
    }
  }
  return 1;
}

int ast_vector_init(mos_allocator *alloc, mos_vector *vec) {
  return mos_vector_init(alloc, vec, sizeof(ast_node_h), 0);
}

static int print_node(ast_pool *pool, ast_node const *node, char *restrict buf, int const sz_,
                      char const *restrict literal) {
  if (sz_ < 0) return -1;
  size_t const sz = (size_t)sz_;

  if ((NULL == node) && (NULL != literal)) {
    return snprintf(buf, sz, "%s", literal);
  }

  switch (node->tag) {

  case ast_eof:    return snprintf(buf, sz, "(eof)");
  case ast_nil:    return snprintf(buf, sz, "(nil)");
  case ast_bool:   return snprintf(buf, sz, "(bool %d)", node->bool_.val);
  case ast_symbol: return snprintf(buf, sz, "(symbol %s)", node->symbol.name);
  case ast_i64:    return snprintf(buf, sz, "(i64 %" PRId64 ")", node->i64.val);
  case ast_u64:    return snprintf(buf, sz, "(u64 %" PRIu64 ")", node->u64.val);
  case ast_f64:    return snprintf(buf, sz, "(f64 %f)", node->f64.val);
  case ast_string: return snprintf(buf, sz, "(string \"%s\")", node->symbol.name);
  case ast_infix:  {
    ast_node *left, *right;
    left       = ast_pool_at(pool, node->infix.left);
    right      = ast_pool_at(pool, node->infix.right);

    int res    = 0;
    int offset = 0;
    res        = snprintf(buf, sz, "(infix %s ", ast_operator_to_string(node->infix.op));
    if (res < 0) return res;
    offset += res;

    res = print_node(pool, left, buf + offset, sz_ - offset, NULL);
    if (res < 0) return res;
    offset += res;

    res = print_node(pool, NULL, buf + offset, sz_ - offset, " ");
    if (res < 0) return res;
    offset += res;

    res = print_node(pool, right, buf + offset, sz_ - offset, NULL);
    if (res < 0) return res;
    offset += res;

    res = print_node(pool, NULL, buf + offset, sz_ - offset, ")");
    if (res < 0) return res;
    offset += res;

  } break;

  case ast_tuple: {
    int res    = 0;
    int offset = 0;

    res        = snprintf(buf, sz, "(tuple");
    if (res < 0) return 1;
    offset += res;

    size_t            count = mos_vector_size(&node->tuple.elements);
    ast_node_h const *it    = mos_vector_cbegin(&node->tuple.elements);
    while (count--) {
      res = print_node(pool, NULL, buf + offset, sz_ - offset, " ");
      if (res < 0) return res;
      offset += res;

      ast_node *el = ast_pool_at(pool, *it);
      res          = print_node(pool, el, buf + offset, sz_ - offset, NULL);
      if (res < 0) return res;
      offset += res;

      ++it;
    }

    res = print_node(pool, NULL, buf + offset, sz_ - offset, ")");
    if (res < 0) return res;
    offset += res;

  } break;
  case ast_let_in: {
  } break;
  case ast_let: {
  } break;
  case ast_if_then_else: {
  } break;
  case ast_lambda_function: {
  } break;
  case ast_function_declaration: {
  } break;
  case ast_lambda_declaration: {
  } break;
  case ast_lambda_function_application: {
  } break;
  case ast_named_function_application: {
  } break;
  }

  return 0;
}

int ast_node_to_string_buf(ast_pool *pool, ast_node const *node, char *buf, size_t sz_) {
  if (sz_ > INT_MAX) return 1;
  int sz  = (int)sz_;

  int res = print_node(pool, node, buf, sz, NULL);

  // check error conditions from snprintf
  if (res < 0 || res > sz) return 1;
  return 0;
}
