#ifndef TESS_TYPE_H
#define TESS_TYPE_H

#include "vector.h"

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TESS_TYPE_TAGS(X)                                                                                  \
    X(type_nil, "nil")                                                                                     \
    X(type_bool, "bool")                                                                                   \
    X(type_int, "int")                                                                                     \
    X(type_float, "float")                                                                                 \
    X(type_string, "string")                                                                               \
    X(type_tuple, "tuple")                                                                                 \
    X(type_arrow, "arrow")                                                                                 \
    X(type_type_var, "type_var")

typedef enum { TESS_TYPE_TAGS(MOS_TAG_NAME) } tess_type_tag;

typedef struct tess_type {
    union {
        struct vector tuple;
        struct {
            u32 left;
            u32 right;
        } arrow;
        u32 val;
    };
    tess_type_tag tag;
} tess_type;

void          tess_type_init(tess_type *, tess_type_tag);
void          tess_type_init_type_var(tess_type *, u32);
nodiscard int tess_type_init_tuple(allocator *, struct tess_type *);
void          tess_type_init_arrow(tess_type *);
void          tess_type_deinit(allocator *, struct tess_type *);

char const   *type_tag_to_string(tess_type_tag);

#endif
