#include "vector.h"

#include "alloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

mos_vector *mos_vector_alloc(mos_allocator *alloc) {
  return alloc->malloc(sizeof(mos_vector));
}

void mos_vector_dealloc(mos_allocator *alloc, mos_vector **p) {
  alloc->free(*p);
  *p = 0;
}

void mos_vector_init(mos_vector *vec, size_t element_size) {
  assert(element_size <= PTRDIFF_MAX);
  memset(vec, 0, sizeof *vec);
  vec->element_size = element_size;
}

void mos_vector_deinit(mos_allocator *alloc, mos_vector *vec) {
  alloc->free(vec->data);
  mos_alloc_invalidate(vec, sizeof *vec);
}

int mos_vector_reserve(mos_allocator *alloc, mos_vector *vec, size_t count) {

  if (vec->capacity >= count) return 0;

  size_t new_capacity = vec->capacity * 2;
  if (new_capacity == 0) new_capacity = 8;
  while (new_capacity < count) new_capacity *= 2;

  // if vec->data is null, this is equivaluent to calling malloc
  void *p = alloc->realloc(vec->data, new_capacity * vec->element_size);
  if (!p) return 1;

  vec->data     = p;
  vec->capacity = new_capacity;
  return 0;
}

void mos_vector_move(mos_vector *dst, mos_vector *src) {
  memcpy(dst, src, sizeof *dst);
  mos_alloc_invalidate(src, sizeof *src);
}

bool mos_vector_empty(mos_vector const *vec) {
  return vec->size == 0;
}

int mos_vector_push_back(mos_allocator *alloc, mos_vector *vec, void const *element) {
  if (mos_vector_reserve(alloc, vec, vec->size + 1)) return 1;
  memcpy(vec->data + vec->size * vec->element_size, element, vec->element_size);
  ++vec->size;
  return 0;
}

int mos_vector_copy_back(mos_allocator *alloc, mos_vector *vec, void const *start, size_t count) {
  if (mos_vector_reserve(alloc, vec, vec->size + count)) return 1;

  memcpy(vec->data + vec->size * vec->element_size, start, count * vec->element_size);
  vec->size += count;
  return 0;
}

void *mos_vector_at(mos_vector *vec, size_t index) {
  return vec->data + index * vec->element_size;
}

void *mos_vector_back(mos_vector *vec) {
  return vec->data + (vec->size - 1) * vec->element_size;
}

void mos_vector_pop_back(mos_vector *vec) {
  --vec->size;
}

void mos_vector_erase(mos_vector *vec, char *it) {
  char const *const end = vec->data + vec->size * vec->element_size;
  ptrdiff_t         len = end - it - (ptrdiff_t)vec->element_size;

  memmove(it, it + vec->element_size, (size_t)len);
  --vec->size;
}

nodiscard int mos_vector_resize(mos_allocator *alloc, mos_vector *vec, size_t n) {

  if (n > vec->capacity)
    if (mos_vector_reserve(alloc, vec, n)) return 1;

  vec->size = n;
  return 0;
}

void mos_vector_clear(mos_vector *vec) {
  // Note: Do not free data.
  vec->size = 0;
}

char *mos_vector_data(mos_vector *vec) {
  return vec->data;
}

void *mos_vector_begin(mos_vector *vec) {
  return vec->data;
}

void const *mos_vector_end(mos_vector *vec) {
  // points 1 past the end
  return mos_vector_at(vec, vec->size);
}

size_t mos_vector_size(mos_vector const *vec) {
  return vec->size;
}

size_t mos_vector_capacity(mos_vector const *vec) {
  return vec->capacity;
}

int mos_vector_assoc_set(mos_allocator *alloc, mos_vector *vec, void const *pair) {
  assert(vec->element_size >= sizeof(size_t));
  if (mos_vector_push_back(alloc, vec, pair)) return 1;
  return 0;
}

char *mos_vector_assoc_get(mos_vector *vec, size_t key) {
  if (mos_vector_empty(vec)) return 0;

  // From the back, search for an element whose first size_t field
  // matches the search term.

  char const *const last         = vec->data;
  char             *it           = mos_vector_back(vec);
  size_t const      element_size = vec->element_size;

  while (1) {
    if (key == *(size_t *)it) return (it + sizeof(size_t)); // return second element of pair

    if (it == last) break; // examined last pair
    it -= element_size;
  }
  return 0;
}

void mos_vector_assoc_erase(mos_vector *vec, size_t key) {

  char *it = mos_vector_assoc_get(vec, key);
  if (!it) return;

  // it points just past the key, so reverse it
  it -= sizeof(size_t);

  mos_vector_erase(vec, it);
}
