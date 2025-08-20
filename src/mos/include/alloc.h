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

// -- utilities --

void  mos_alloc_invalidate(void *, size_t);
char *mos_alloc_strdup(mos_allocator *, char const *);
char *mos_alloc_strndup(mos_allocator *, char const *, size_t);

#endif
