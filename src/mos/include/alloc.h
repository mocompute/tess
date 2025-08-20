#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include <stdlib.h>

typedef struct mos_allocator {
  void *(*malloc)(size_t);
  void *(*calloc)(size_t num, size_t size);
  void *(*realloc)(void *, size_t);
  void (*free)(void *);
} mos_allocator;

// Return the default allocator: system's malloc/free
mos_allocator *mos_alloc_default_allocator();

mos_allocator *mos_alloc_arena_alloc(mos_allocator *parent);
void           mos_alloc_arena_dealloc(mos_allocator *parent, mos_allocator **arena);
int            mos_alloc_arena_init(mos_allocator *, mos_allocator *parent, size_t);
void           mos_alloc_deinit(mos_allocator *);

// -- utilities --

void   mos_alloc_invalidate(void *, size_t);
char  *mos_alloc_strdup(mos_allocator *, char const *);
char  *mos_alloc_strndup(mos_allocator *, char const *, size_t);

size_t mos_alloc_next_power_of_two(size_t);

#endif
