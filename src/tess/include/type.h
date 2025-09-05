#ifndef TESS_TYPE_H
#define TESS_TYPE_H

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
    X(type_labelled_tuple, "labelled_tuple")                                                               \
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
            tl_type_sized   fields;
            c_string_csized names;
        }; // labelled_tuple

        struct {
            struct tl_type *left;
            struct tl_type *right;
        }; // arrow

        struct {
            char const     *name;
            struct tl_type *labelled_tuple;
        }; // user

        u32 type_var;
    };
    tl_type_tag tag;
} tl_type;

typedef struct {
    array_header;
    struct tl_type **v;
} tl_type_array;

nodiscard tl_type *tl_type_create_type_var(allocator *, u32) mallocfun;
nodiscard tl_type *tl_type_create_tuple(allocator *, tl_type_sized) mallocfun;
nodiscard tl_type *tl_type_create_labelled_tuple(allocator *, tl_type_sized, c_string_csized) mallocfun;
nodiscard tl_type *tl_type_create_arrow(allocator *, tl_type *, tl_type *) mallocfun;
nodiscard tl_type *tl_type_create_user_type(allocator *, char const *name,
                                            tl_type *labelled_tuple) mallocfun;

bool               tl_type_is_prim(tl_type const *);
bool               tl_type_equal(tl_type const *, tl_type const *);
int                tl_type_compare(tl_type const *, tl_type const *);
bool               tl_type_satisfies(tl_type const *req, tl_type const *cand);

int                tl_type_snprint(char *, int, tl_type const *);
char              *tl_type_to_string(allocator *, tl_type const *);
char const        *type_tag_to_string(tl_type_tag);

#endif
