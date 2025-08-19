#include "ast.h"
#include "vector.h"

#include <string.h>

// -- tess_type allocation and deallocation --

void tess_type_init(tess_type_t *ty, tess_type_tag_t tag) {
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
}

// -- tess_type_pool allocation and deallocation --

tess_type_pool_t *tess_type_pool_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(tess_type_pool_t));
}

void tess_type_pool_dealoc(mos_allocator_t *alloc, tess_type_pool_t *pool) {
  alloc->free(pool);
}

void tess_type_pool_init(mos_allocator_t *alloc, tess_type_pool_t *pool) {
  mos_vector_init(&pool->data, sizeof(tess_type_t));
  mos_vector_reserve(alloc, &pool->data, 32);
}

void tess_type_pool_deinit(mos_allocator_t *alloc, tess_type_pool_t *pool) {
  mos_vector_deinit(alloc, &pool->data);
}
