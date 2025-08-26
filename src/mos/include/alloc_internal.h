#ifndef MOS_ALLOC_INTERNAL_H
#define MOS_ALLOC_INTERNAL_H

#include <stddef.h>

struct allocator {
    void *(*malloc)(struct allocator *, size_t, char const *, int);
    void *(*calloc)(struct allocator *, size_t num, size_t size, char const *, int);
    void *(*realloc)(struct allocator *, void *, size_t, char const *, int);
    void (*free)(struct allocator *, void *, char const *, int);
};

#endif
