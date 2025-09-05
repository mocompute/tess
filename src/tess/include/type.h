#ifndef TYPE_H
#define TYPE_H

#include "alloc.h"
#include "array.h"
#include "types.h"

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TL_TYPE_TAGS(X)                                                                                    \
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

typedef enum { TL_TYPE_TAGS(MOS_TAG_NAME) } tl_type_tag;

typedef struct {
    array_sized;
    struct tl_type **v;
} tl_type_sized;

typedef struct tl_type {
    union {
        struct {
            tl_type_sized elements;
        }; // tuple

        struct {
            struct tl_type *left;
            struct tl_type *right;
        }; // arrow

        struct {
            // TODO this could more helpfully be a tuple type instead of an array of fields.
            char const        *name;
            struct tl_type **fields;
            char const       **field_names;
            u16                n_fields;
        }; // user

        u32 type_var;
    };
    tl_type_tag tag;
} tl_type;

typedef struct {
    array_header;
    tl_type **v;
} tl_type_array;

tl_type   tl_type_init(tl_type_tag);
tl_type   tl_type_init_type_var(u32);
tl_type   tl_type_init_tuple();
tl_type   tl_type_init_arrow(tl_type *, tl_type *);

tl_type   tl_type_init_user_type(char const *name, tl_type **fields, char const **field_names, u16 n);

void        tl_type_deinit(allocator *, tl_type *);

tl_type  *tl_type_create_type_var(allocator *, u32) mallocfun;
tl_type  *tl_type_create_tuple(allocator *, u16) mallocfun;
tl_type  *tl_type_create_arrow(allocator *, tl_type *, tl_type *) mallocfun;
tl_type  *tl_type_create_user_type(allocator *, char const *name, tl_type **fields,
                                       char const **field_names, u16 n) mallocfun;

tl_type  *tl_type_prim(tl_type_tag); // only primitives

bool        tl_type_is_prim(tl_type const *);
bool        tl_type_equal(tl_type const *, tl_type const *);
int         tl_type_compare(tl_type const *, tl_type const *);

int         tl_type_snprint(char *, int, tl_type const *);
char       *tl_type_to_string(allocator *, tl_type const *);
char const *type_tag_to_string(tl_type_tag);

#endif
