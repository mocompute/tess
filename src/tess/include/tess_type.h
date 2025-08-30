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
    X(type_user, "user")                                                                                   \
    X(type_type_var, "type_var")

typedef enum { TESS_TYPE_TAGS(MOS_TAG_NAME) } tess_type_tag;

struct tess_type {
    union {
        struct {
            struct tess_type const **elements;
            u16                      n_elements;
        };

        struct {
            struct tess_type const *left;
            struct tess_type const *right;
        };

        struct {
            char const              *name;
            struct tess_type const **fields;
            char const             **field_names;
            u16                      n_fields;
        };

        u32 type_var;
    };
    tess_type_tag tag;
};

struct tess_type_iterator {
    struct vector_iterator_base base;
    struct tess_type          **ptr;
};

struct tess_type  tess_type_init(tess_type_tag);
struct tess_type  tess_type_init_type_var(u32);
struct tess_type  tess_type_init_tuple();
struct tess_type  tess_type_init_arrow(struct tess_type *, struct tess_type *);

struct tess_type  tess_type_init_user_type(char const *name, struct tess_type const **fields,
                                           char const **field_names, u16 n);

void              tess_type_deinit(allocator *, struct tess_type *);

struct tess_type *tess_type_create_type_var(allocator *, u32) mallocfun;
struct tess_type *tess_type_create_tuple(allocator *, u16) mallocfun;
struct tess_type *tess_type_create_arrow(allocator *, struct tess_type *, struct tess_type *) mallocfun;
struct tess_type *tess_type_create_user_type(allocator *, char const *name, struct tess_type const **fields,
                                             char const **field_names, u16 n) mallocfun;

struct tess_type const *tess_type_prim(tess_type_tag); // only primitives

bool                    tess_type_is_prim(struct tess_type const *);
bool                    tess_type_equal(struct tess_type const *, struct tess_type const *);
int                     tess_type_compare(struct tess_type const *, struct tess_type const *);

int                     tess_type_snprint(char *, int, struct tess_type const *);
char                   *tess_type_to_string(allocator *, struct tess_type const *);
char const             *type_tag_to_string(tess_type_tag);

#endif
