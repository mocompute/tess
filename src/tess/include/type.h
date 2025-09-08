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
        struct tlt_array {
            tl_type_sized elements;
        } array;

        struct tlt_tuple {
            tl_type_sized elements;
        } tuple;

        struct tlt_labelled_tuple {
            tl_type_sized   fields;
            c_string_csized names;
        } labelled_tuple;

        struct tlt_arrow {
            struct tl_type *left;
            struct tl_type *right;
        } arrow;

        struct tlt_user {
            char const     *name;
            struct tl_type *labelled_tuple;
        } user;

        struct tlt_tv {
            u32 val;
        } type_var;
    };
    tl_type_tag tag;
} tl_type;

typedef struct {
    array_header;
    struct tl_type **v;
} tl_type_array;

// -- variant access --

struct tlt_array          *tl_type_arr(tl_type *);
struct tlt_tuple          *tl_type_tup(tl_type *);
struct tlt_labelled_tuple *tl_type_lt(tl_type *);
struct tlt_arrow          *tl_type_arrow(tl_type *);
struct tlt_user           *tl_type_user(tl_type *);
struct tlt_tv             *tl_type_tv(tl_type *);

// -- allocation --

nodiscard tl_type *tl_type_create_type_var(allocator *, u32) mallocfun;
nodiscard tl_type *tl_type_create_tuple(allocator *, tl_type_sized) mallocfun;
nodiscard tl_type *tl_type_create_labelled_tuple(allocator *, tl_type_sized, c_string_csized) mallocfun;
nodiscard tl_type *tl_type_create_arrow(allocator *, tl_type *, tl_type *) mallocfun;
nodiscard tl_type *tl_type_create_user_type(allocator *, char const *name,
                                            tl_type *labelled_tuple) mallocfun;

bool               tl_type_is_prim(tl_type const *);
bool               tl_type_is_poly(tl_type const *);
bool               tl_type_equal(tl_type const *, tl_type const *);
int                tl_type_compare(tl_type const *, tl_type const *);
bool               tl_type_satisfies(tl_type const *req, tl_type const *cand);
bool               tl_type_contains(tl_type const *, tl_type const *);
u64                tl_type_hash(tl_type const *);
u64                tl_type_hash_ext(tl_type const *self, bool ignore_names);

tl_type           *tl_type_find_user_field_type(tl_type const *, char const *);
tl_type           *tl_type_find_labelled_field_type(tl_type const *, char const *);

int                tl_type_snprint(char *, int, tl_type const *);
char              *tl_type_to_string(allocator *, tl_type const *);
char const        *tl_type_tag_to_string(tl_type_tag);

#endif
