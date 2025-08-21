#include "alloc.h"

#include <assert.h>
#include <stdalign.h>
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

static void *default_malloc(mos_allocator *a, size_t sz) {
  (void)a;
  return malloc(sz);
}

static void *default_calloc(mos_allocator *a, size_t num, size_t sz) {
  (void)a;
  return calloc(num, sz);
}

static void *default_realloc(mos_allocator *a, void *p, size_t sz) {
  (void)a;
  return realloc(p, sz);
}

static void default_free(mos_allocator *a, void *p) {
  (void)a;
  return free(p);
}

mos_allocator *mos_alloc_default_allocator() {
  static mos_allocator allocator = {&default_malloc, &default_calloc, &default_realloc, &default_free};
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

static_assert(48 == sizeof(arena_allocator), "");

static void *bump_alloc_assume_capacity(arena_header *bucket, size_t sz) {
  char *out      = ((char *)bucket) + sizeof(arena_header) + bucket->size;
  *(size_t *)out = sz;
  bucket->size += sz + sizeof(size_t);
  return out + sizeof(size_t);
}

static size_t *block_size(char const *p) {
  return (size_t *)(p - sizeof(size_t));
}

static bool is_last_block(arena_header const *bucket, void const *p) {
  size_t sz = *block_size(p);
  if (((char *)p) < ((char *)bucket)) return false;
  return (size_t)(((char *)p) - ((char *)bucket)) + sz == bucket->size;
}

static void maybe_free_block(arena_header *bucket, void const *ptr) {
  if (is_last_block(bucket, ptr)) {
    bucket->size -= *block_size(ptr) + sizeof(size_t);
  }
}

static arena_header *find_bucket(arena_allocator const *arena, void const *ptr) {
  arena_header *bucket = arena->head;
  assert(bucket);
  while (bucket) {
    if (((char *)ptr) > ((char *)bucket) + sizeof(arena_header) &&
        ((char *)ptr) < ((char *)bucket + sizeof(arena_header) + bucket->size))
      return bucket;

    bucket = bucket->next;
  }

  return NULL;
}

static void *arena_malloc(mos_allocator *alloc, size_t sz) {
  arena_allocator *arena         = (arena_allocator *)alloc;

  arena_header    *bucket        = arena->head;
  arena_header    *last          = NULL;
  size_t           last_capacity = 0;

  sz                             = mos_alloc_align_to_word_size(sz);

  assert(bucket);
  while (bucket) {
    if (bucket->capacity - bucket->size >= sz) {
      return bump_alloc_assume_capacity(bucket, sz);
    }

    last_capacity = bucket->capacity;
    last          = bucket;
    bucket        = bucket->next;
  }

  // need to allocate a new bucket

  size_t new_capacity = last_capacity * 2;
  if (new_capacity < sz) new_capacity = mos_alloc_next_power_of_two(sz);

  last->next = arena->parent->malloc(arena->parent, new_capacity + sizeof(arena_header));
  if (NULL == last->next) return NULL;

  bucket = last->next;

  memset(bucket, 0, sizeof *bucket);
  bucket->capacity = new_capacity;
  return bump_alloc_assume_capacity(bucket, sz);
}

static void *arena_realloc(mos_allocator *a, void *p, size_t sz) {
  if (NULL == p) return arena_malloc(a, sz);

  arena_header *bucket = find_bucket((arena_allocator *)a, p);
  if (NULL == bucket) return NULL;

  size_t *cur_size_p = block_size(p);
  size_t  cur_size   = *cur_size_p;
  if (sz == cur_size) return p;

  if (is_last_block(bucket, p)) {

    // shrink block in place
    if (sz < cur_size) {
      *cur_size_p = sz;
      bucket->size -= cur_size - sz;
      return p;
    }

    // grow block if there is room in its bucket
    if (sz <= (bucket->capacity - bucket->size) + cur_size) {
      *cur_size_p = sz;
      bucket->size += sz - cur_size;
      return p;
    }
  }

  // need to allocate a new block, copy data and release old block if
  // possible

  void *new_block = arena_malloc(a, sz);
  assert(sz >= cur_size);
  memcpy(new_block, p, cur_size);
  maybe_free_block(bucket, p);
  return new_block;
}

static void *arena_calloc(mos_allocator *alloc, size_t num, size_t size) {
  void *out = arena_malloc(alloc, num * size);
  if (out) memset(out, 0, num * size);
  return out;
}

static void arena_free(mos_allocator *alloc, void *p) {
  (void)alloc;
  (void)p;
}

mos_allocator *mos_alloc_arena_alloc(mos_allocator *alloc) {
  return alloc->malloc(alloc, sizeof(arena_allocator));
}

mos_allocator *mos_alloc_arena_alloci(mos_allocator *alloc, size_t sz) {
  mos_allocator *out = alloc->malloc(alloc, sizeof(arena_allocator));
  if (!out) return out;

  if (mos_alloc_arena_init(out, alloc, sz)) {
    alloc->free(alloc, out);
    return NULL;
  }
  return out;
}

void mos_alloc_arena_dealloc(mos_allocator *alloc, mos_allocator **arena) {
  mos_alloc_assert_invalid(*arena, sizeof **arena);
  alloc->free(alloc, *arena);
  *arena = NULL;
}

void mos_alloc_arena_dealloci(mos_allocator *alloc, mos_allocator **arena) {
  mos_alloc_arena_deinit(*arena);
  mos_alloc_arena_dealloc(alloc, arena);
}

int mos_alloc_arena_init(mos_allocator *arena_, mos_allocator *parent, size_t sz) {
  arena_allocator *arena = (arena_allocator *)arena_;
  arena->parent          = parent;
  sz                     = mos_alloc_next_power_of_two(sz);
  if (0 == sz) return 1;
  arena->head = parent->malloc(parent, sizeof(arena_header) + sz);
  if (NULL == arena->head) return 1;

  memset(arena->head, 0, sizeof *arena->head);
  arena->head->capacity    = sz;

  arena->allocator.malloc  = &arena_malloc;
  arena->allocator.calloc  = &arena_calloc;
  arena->allocator.realloc = &arena_realloc;
  arena->allocator.free    = &arena_free;
  return 0;
}

void mos_alloc_arena_deinit(mos_allocator *arena_) {
  arena_allocator *arena = (arena_allocator *)arena_;

  arena_header    *next  = arena->head;

  while (next) {
    arena_header *next_next = next->next;
    arena->parent->free(arena->parent, next);
    next = next_next;
  }

  mos_alloc_invalidate(arena, sizeof *arena);
}

// -- utilities --

char *mos_alloc_strdup(mos_allocator *alloc, char const *src) {
  size_t len = strlen(src);
  char  *out = alloc->malloc(alloc, len + 1);
  if (out) {
    memcpy(out, src, len);
    out[len] = '\0';
  }
  return out;
}

char *mos_alloc_strndup(mos_allocator *alloc, char const *src, size_t max) {
  size_t len = strlen(src);
  if (len > max) len = max;
  char *out = alloc->malloc(alloc, len + 1);
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

void mos_alloc_assert_invalid(void *p, size_t len) {
#ifndef NDEBUG
  while (len--) {
    if ((intptr_t)p % 2 == 0) assert(*(unsigned char *)p == 0xde);
    else assert(*(unsigned char *)p == 0xad);
    ++p;
  }
#else
  (void)p;
  (void)len;
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

size_t mos_alloc_align_to_word_size(size_t n) {
  size_t mask = sizeof(void *) - 1;
  return (n + mask) & ~mask;
}
