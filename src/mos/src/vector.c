#include "vector.h"

#include "alloc.h"

#include <string.h>

struct mos_vector_t {
  size_t element_size;
  size_t capacity;
  size_t size;
  char *data;
};

mos_vector_t *mos_vector_alloc(mos_allocator_t *alloc) {
  return alloc->malloc(sizeof(mos_vector_t));
}

void mos_vector_dealloc(mos_allocator_t *alloc, mos_vector_t *p) { alloc->free(p); }

void mos_vector_init(mos_vector_t *vec, size_t element_size) {
  memset(vec, 0, sizeof *vec);
  vec->element_size = element_size;
}

void mos_vector_deinit(mos_allocator_t *alloc, mos_vector_t *vec) {
  alloc->free(vec->data);
}

int mos_vector_reserve(mos_allocator_t *alloc, mos_vector_t *vec, size_t count) {

  if (vec->capacity >= count) return 0;

  void *p = alloc->realloc(vec->data, count * vec->element_size);
  if (!p) return 1;

  vec->data = p;
  vec->capacity = count;
  return 0;
}

int mos_vector_empty(mos_vector_t *vec) { return vec->size == 0; }

int mos_vector_push_back(mos_allocator_t *alloc, mos_vector_t *vec, void *element) {
  if (mos_vector_reserve(alloc, vec, vec->size + 1)) return 1;
  memcpy(vec->data + vec->size * vec->element_size, element, vec->element_size);
  ++vec->size;
  return 0;
}

int mos_vector_copy_back(mos_allocator_t *alloc, mos_vector_t *vec, void *start,
                         size_t count) {
  if (mos_vector_reserve(alloc, vec, vec->size + count)) return 1;

  memcpy(vec->data + vec->size * vec->element_size, start, count * vec->element_size);
  vec->size += count;
  return 0;
}

void *mos_vector_back(mos_vector_t *vec) {
  return vec->data + (vec->size - 1) * vec->element_size;
}

void mos_vector_pop_back(mos_vector_t *vec) { --vec->size; }

void *mos_vector_data(mos_vector_t *vec) { return vec->data; }
size_t mos_vector_size(mos_vector_t *vec) { return vec->size; }
size_t mos_vector_capacity(mos_vector_t *vec) { return vec->capacity; }
