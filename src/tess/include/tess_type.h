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
            struct tess_type *left;
            struct tess_type *right;
        } arrow;

        u32 type_var;
    };
    tess_type_tag tag;
} tess_type;

tess_type   tess_type_init(tess_type_tag);
tess_type   tess_type_init_type_var(u32);
tess_type   tess_type_init_tuple(allocator *);
tess_type   tess_type_init_arrow(tess_type *, tess_type *);
void        tess_type_deinit(allocator *, tess_type *);

int         tess_type_snprint(char *, int, tess_type const *);
char       *tess_type_to_string(allocator *, tess_type const *);

char const *type_tag_to_string(tess_type_tag);

#endif
