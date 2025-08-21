#include "vector.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

vec_t *vec_alloc(mos_allocator *alloc) {
  return alloc->malloc(alloc, sizeof(vec_t));
}

void vec_dealloc(mos_allocator *alloc, vec_t **p) {
  alloc->free(alloc, *p);
  *p = 0;
}

int vec_init(mos_allocator *alloc, vec_t *vec, size_t element_size, size_t initial_size) {
  assert(element_size <= PTRDIFF_MAX);
  memset(vec, 0, sizeof *vec);
  vec->element_size = element_size;

  if (initial_size) {
    vec->data = alloc->malloc(alloc, initial_size * element_size);
    if (NULL == vec->data) {
      dbg("vec_init: oom\n");
      return 1;
    }
    vec->capacity = initial_size;
  }
  return 0;
}

void vec_deinit(mos_allocator *alloc, vec_t *vec) {
  alloc->free(alloc, vec->data);
  mos_alloc_invalidate(vec, sizeof *vec);
}

int vec_reserve(mos_allocator *alloc, vec_t *vec, size_t count) {

  if (vec->capacity >= count) return 0;

  size_t new_capacity = vec->capacity * 2;
  if (new_capacity == 0) new_capacity = 8;
  while (new_capacity < count) new_capacity *= 2;

  // if vec->data is null, this is equivaluent to calling malloc
  void *p = alloc->realloc(alloc, vec->data, new_capacity * vec->element_size);
  if (!p) {
    dbg("vec_reserve: oom\n");
    return 1;
  }

  vec->data     = p;
  vec->capacity = new_capacity;
  return 0;
}

void vec_move(vec_t *dst, vec_t *src) {
  memcpy(dst, src, sizeof *dst);
  mos_alloc_invalidate(src, sizeof *src);
}

bool vec_empty(vec_t const *vec) {
  return vec->size == 0;
}

int vec_push_back(mos_allocator *alloc, vec_t *vec, void const *element) {
  if (vec_reserve(alloc, vec, vec->size + 1)) {
    dbg("vec_push_back: oom\n");
    return 1;
  }
  memcpy(vec->data + vec->size * vec->element_size, element, vec->element_size);
  ++vec->size;
  return 0;
}

int vec_copy_back(mos_allocator *alloc, vec_t *vec, void const *start, size_t count) {
  if (vec_reserve(alloc, vec, vec->size + count)) {
    dbg("vec_copy_back: oom\n");
    return 1;
  }

  memcpy(vec->data + vec->size * vec->element_size, start, count * vec->element_size);
  vec->size += count;
  return 0;
}

void *vec_at(vec_t *vec, size_t index) {
  return vec->data + index * vec->element_size;
}

void *vec_back(vec_t *vec) {
  return vec->data + (vec->size - 1) * vec->element_size;
}

void vec_pop_back(vec_t *vec) {
  --vec->size;
}

void vec_erase(vec_t *vec, char *it) {
  char const *const end = vec->data + vec->size * vec->element_size;
  ptrdiff_t         len = end - it - (ptrdiff_t)vec->element_size;

  memmove(it, it + vec->element_size, (size_t)len);
  --vec->size;
}

nodiscard int vec_resize(mos_allocator *alloc, vec_t *vec, size_t n) {

  if (n > vec->capacity)
    if (vec_reserve(alloc, vec, n)) {
      dbg("vec_resize: oom\n");
      return 1;
    }

  vec->size = n;
  return 0;
}

void vec_clear(vec_t *vec) {
  // Note: Do not free data.
  vec->size = 0;
}

char *vec_data(vec_t *vec) {
  return vec->data;
}

void *vec_begin(vec_t *vec) {
  return vec->data;
}

void const *vec_cbegin(vec_t const *vec) {
  return vec->data;
}

void const *vec_end(vec_t *vec) {
  // points 1 past the end
  return vec_at(vec, vec->size);
}

size_t vec_size(vec_t const *vec) {
  return vec->size;
}

size_t vec_capacity(vec_t const *vec) {
  return vec->capacity;
}

int vec_assoc_set(mos_allocator *alloc, vec_t *vec, void const *pair) {
  assert(vec->element_size >= sizeof(size_t));
  if (vec_push_back(alloc, vec, pair)) {
    dbg("vec_assoc_set: oom\n");
    return 1;
  }
  return 0;
}

char *vec_assoc_get(vec_t *vec, size_t key) {
  if (vec_empty(vec)) return 0;

  // From the back, search for an element whose first size_t field
  // matches the search term.

  char const *const last         = vec->data;
  char             *it           = vec_back(vec);
  size_t const      element_size = vec->element_size;

  while (1) {
    if (key == *(size_t *)it) return (it + sizeof(size_t)); // return second element of pair

    if (it == last) break; // examined last pair
    it -= element_size;
  }
  return 0;
}

void vec_assoc_erase(vec_t *vec, size_t key) {

  char *it = vec_assoc_get(vec, key);
  if (!it) return;

  // it points just past the key, so reverse it
  it -= sizeof(size_t);

  vec_erase(vec, it);
}
