#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include "nodiscard.h"
#include <stdlib.h>

typedef struct allocator {
  void *(*malloc)(struct allocator *, size_t);
  void *(*calloc)(struct allocator *, size_t num, size_t size);
  void *(*realloc)(struct allocator *, void *, size_t);
  void (*free)(struct allocator *, void *);
} allocator;

typedef struct arena_header {
  struct arena_header *next;
  size_t               capacity;
  size_t               size;
} arena_header;

typedef struct arena_allocator {
  struct allocator allocator;
  allocator       *parent;
  arena_header    *head;
} arena_allocator;

// Return the default allocator: system's malloc/free
allocator    *alloc_default_allocator();

allocator    *alloc_arena_alloc(allocator *parent);
allocator    *alloc_arena_alloci(allocator *alloc, size_t);
void          alloc_arena_dealloc(allocator *parent, allocator **arena);
void          alloc_arena_dealloci(allocator *, allocator **);
nodiscard int alloc_arena_init(allocator *, allocator *parent, size_t);
void          alloc_arena_deinit(allocator *);

// -- utilities --

void   alloc_invalidate(void *, size_t);
void   alloc_assert_invalid(void *, size_t);

char  *alloc_strdup(allocator *, char const *);
char  *alloc_strndup(allocator *, char const *, size_t);

size_t alloc_next_power_of_two(size_t);
size_t alloc_align_to_word_size(size_t);

#endif
