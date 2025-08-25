#ifndef TESS_TYPE_H
#define TESS_TYPE_H

#include "util.h"
#include "vector.h"

#define TESS_TYPE_TAGS(X)                                                                                  \
    X(type_nil, "nil")                                                                                     \
    X(type_bool, "bool")                                                                                   \
    X(type_int, "int")                                                                                     \
    X(type_float, "float")                                                                                 \
    X(type_string, "string")                                                                               \
    X(type_tuple, "tuple")                                                                                 \
    X(type_arrow, "arrow")                                                                                 \
    X(type_type_var, "type_var")

typedef enum type_tag { TESS_TYPE_TAGS(MOS_TAG_NAME) } type_tag;

typedef struct tess_type {
    union {
        struct vector tuple;
        struct {
            u32 left;
            u32 right;
        } arrow;
        u32 val;
    };
    type_tag tag;
} tess_type;

typedef struct {
    u32 val;
} tess_type_h;

typedef struct tess_type_pool tess_type_pool;

void                          tess_type_init(tess_type *, type_tag);
void                          tess_type_init_type_var(tess_type *, u32);
nodiscard int                 tess_type_init_tuple(allocator *, tess_type *);
void                          tess_type_init_arrow(tess_type *);
void                          tess_type_deinit(allocator *, tess_type *);

tess_type_pool               *tess_type_pool_create(allocator *) mallocfun;
void                          tess_type_pool_destroy(tess_type_pool **);

nodiscard int                 tess_type_pool_move_back(tess_type_pool *, tess_type *, tess_type_h *);

char const                   *type_tag_to_string(type_tag);

#endif
