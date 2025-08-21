#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include "nodiscard.h"
#include <stdlib.h>

typedef struct mos_allocator {
  void *(*malloc)(struct mos_allocator *, size_t);
  void *(*calloc)(struct mos_allocator *, size_t num, size_t size);
  void *(*realloc)(struct mos_allocator *, void *, size_t);
  void (*free)(struct mos_allocator *, void *);
} mos_allocator;

// Return the default allocator: system's malloc/free
mos_allocator *mos_alloc_default_allocator();

mos_allocator *mos_alloc_arena_alloc(mos_allocator *parent);
mos_allocator *mos_alloc_arena_alloci(mos_allocator *alloc, size_t);
void           mos_alloc_arena_dealloc(mos_allocator *parent, mos_allocator **arena);
void           mos_alloc_arena_dealloci(mos_allocator *, mos_allocator **);
nodiscard int  mos_alloc_arena_init(mos_allocator *, mos_allocator *parent, size_t);
void           mos_alloc_arena_deinit(mos_allocator *);

// -- utilities --

void   mos_alloc_invalidate(void *, size_t);
void   mos_alloc_assert_invalid(void *, size_t);

char  *mos_alloc_strdup(mos_allocator *, char const *);
char  *mos_alloc_strndup(mos_allocator *, char const *, size_t);

size_t mos_alloc_next_power_of_two(size_t);
size_t mos_alloc_align_to_word_size(size_t);

#endif
