#ifndef TESS_TYPE_POOL_H
#define TESS_TYPE_POOL_H

#include "alloc.h"
#include "types.h"
#include "vector.h"

typedef struct tess_type tess_type;

struct tess_type_pool {
    allocator    *alloc;
    struct vector data; // tess_type
};

typedef struct {
    u32 val;
} tess_type_h;

typedef struct tess_type_pool tess_type_pool;

// -- allocation and deallocation --

tess_type_pool *tess_type_pool_create(allocator *) mallocfun;
void            tess_type_pool_destroy(tess_type_pool **);

// -- operations --

nodiscard int tess_type_pool_move_back(tess_type_pool *, tess_type *, tess_type_h *);

#endif
