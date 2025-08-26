#ifndef MOS_ALLOC_STRING_H
#define MOS_ALLOC_STRING_H

#include "alloc.h"
#include "nodiscard.h"

nodiscard allocator *alloc_string_arena_create(allocator *, size_t) mallocfun;
void                 alloc_string_arena_destroy(allocator *, allocator **);

#endif
