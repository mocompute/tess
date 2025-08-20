#include "alloc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// use LSAN's allocators if we can detect they are present
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#else
#include <stdlib.h>
#endif
#else
#include <stdlib.h>
#endif

mos_allocator *mos_alloc_default_allocator() {
  static mos_allocator allocator = {&malloc, &calloc, &realloc, &free};
  return &allocator;
}

// -- arena --

typedef struct arena_header {
  struct arena_header *next;
  size_t               capacity;
  size_t               size;
} arena_header;

typedef struct arena_allocator {
  struct mos_allocator allocator;
  mos_allocator       *parent;
  arena_header        *head;
} arena_allocator;

mos_allocator *mos_alloc_arena_alloc(mos_allocator *alloc) {
  return alloc->malloc(sizeof(arena_allocator));
}

void mos_alloc_arena_dealloc(mos_allocator *alloc, mos_allocator **arena) {
  alloc->free(*arena);
  *arena = NULL;
}

int mos_alloc_arena_init(mos_allocator *arena_, mos_allocator *parent, size_t sz) {
  arena_allocator *arena = (arena_allocator *)arena_;
  arena->parent          = parent;
  sz                     = mos_alloc_next_power_of_two(sz);
  if (0 == sz) return 1;
  arena->head = parent->malloc(sizeof(arena_header) + sz);
  if (NULL == arena->head) return 1;
}

// -- utilities --

char *mos_alloc_strdup(mos_allocator *alloc, char const *src) {
  size_t len = strlen(src);
  char  *out = alloc->malloc(len + 1);
  if (out) {
    memcpy(out, src, len);
    out[len] = '\0';
  }
  return out;
}

char *mos_alloc_strndup(mos_allocator *alloc, char const *src, size_t max) {
  size_t len = strlen(src);
  if (len > max) len = max;
  char *out = alloc->malloc(len + 1);
  if (out) {
    memcpy(out, src, len);
    out[len] = '\0';
  }
  return out;
}

void mos_alloc_invalidate(void *p, size_t len) {
#ifndef NDEBUG
  while (len--) {
    if ((intptr_t)p % 2 == 0) *(unsigned char *)p = 0xde;
    else *(unsigned char *)p = 0xad;
    ++p;
  }
#else
  memset(p, 0, len);
#endif
}

// Returns: input if already a power of two, or else the next higher
// power of two.
size_t mos_alloc_next_power_of_two(size_t n) {

  if (n > (SIZE_MAX / 2) + 1) return 0; // overflow
  if (n == 0) return 1;

  // set all bits to the right of the highest set bit by masking.
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return n + 1;
}
