#ifndef MOS_ALLOC_H
#define MOS_ALLOC_H

#include <stdlib.h>

typedef struct mos_allocator_t {
  void *(*malloc)(size_t);
  void *(*calloc)(size_t num, size_t size);
  void *(*realloc)(void *, size_t);
  void (*free)(void *);
} mos_allocator_t;

/// Return the default allocator: system's malloc/free
mos_allocator_t *mos_alloc_default_allocator();

#endif
