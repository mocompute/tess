#ifndef TESS_TYPE_H
#define TESS_TYPE_H

#include "alloc.h"
#include "array.h"
#include "types.h"

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
    X(type_user, "user")                                                                                   \
    X(type_type_var, "type_var")                                                                           \
    X(type_any, "any")

typedef enum { TESS_TYPE_TAGS(MOS_TAG_NAME) } tess_type_tag;

typedef struct {
    array_sized;
    struct tess_type **v;
} tess_type_sized;

typedef struct tess_type {
    union {
        struct {
            tess_type_sized elements;
        }; // tuple

        struct {
            struct tess_type *left;
            struct tess_type *right;
        }; // arrow

        struct {
            char const        *name;
            struct tess_type **fields;
            char const       **field_names;
            u16                n_fields;
        }; // user

        u32 type_var;
    };
    tess_type_tag tag;
} tess_type;

typedef struct {
    array_header;
    tess_type **v;
} tess_type_array;

tess_type   tess_type_init(tess_type_tag);
tess_type   tess_type_init_type_var(u32);
tess_type   tess_type_init_tuple();
tess_type   tess_type_init_arrow(tess_type *, tess_type *);

tess_type   tess_type_init_user_type(char const *name, tess_type **fields, char const **field_names, u16 n);

void        tess_type_deinit(allocator *, tess_type *);

tess_type  *tess_type_create_type_var(allocator *, u32) mallocfun;
tess_type  *tess_type_create_tuple(allocator *, u16) mallocfun;
tess_type  *tess_type_create_arrow(allocator *, tess_type *, tess_type *) mallocfun;
tess_type  *tess_type_create_user_type(allocator *, char const *name, tess_type **fields,
                                       char const **field_names, u16 n) mallocfun;

tess_type  *tess_type_prim(tess_type_tag); // only primitives

bool        tess_type_is_prim(tess_type const *);
bool        tess_type_equal(tess_type const *, tess_type const *);
int         tess_type_compare(tess_type const *, tess_type const *);

int         tess_type_snprint(char *, int, tess_type const *);
char       *tess_type_to_string(allocator *, tess_type const *);
char const *type_tag_to_string(tess_type_tag);

#endif
