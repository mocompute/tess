#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "vector.h"

#include <stdint.h>

#define TESS_TYPE_TAGS(X)                                                                                  \
  X(tess_ty_nil, "nil")                                                                                    \
  X(tess_ty_bool, "bool")                                                                                  \
  X(tess_ty_int, "int")                                                                                    \
  X(tess_ty_float, "float")                                                                                \
  X(tess_ty_string, "string")                                                                              \
  X(tess_ty_tuple, "tuple")                                                                                \
  X(tess_ty_arrow, "arrow")                                                                                \
  X(tess_ty_type_var, "type_var")

#define TESS_ENUM(name, str) name,
typedef enum tess_type_tag { TESS_TYPE_TAGS(TESS_ENUM) } tess_type_tag_t;
#undef TESS_ENUM

struct arrow_type {
  size_t left;
  size_t right;
};

typedef struct tess_type {
  union {
    mos_vector_t      tuple;
    struct arrow_type arrow;
    uint32_t          val;
  };
  tess_type_tag_t tag;
} tess_type_t;

typedef struct tess_type_pool {
  mos_vector_t data; // tess_type_t
} tess_type_pool_t;

// -- allocation and deallocation --

// tess_type_t

void tess_type_init(tess_type_t *, tess_type_tag_t);
void tess_type_init_type_var(tess_type_t *, uint32_t);
void tess_type_init_tuple(tess_type_t *);
void tess_type_init_arrow(tess_type_t *);
void tess_type_deinit(mos_allocator_t *, tess_type_t *);

// tess_type_pool_t

tess_type_pool_t *tess_type_pool_alloc(mos_allocator_t *);
void              tess_type_pool_dealoc(mos_allocator_t *, tess_type_pool_t *);
void              tess_type_pool_init(mos_allocator_t *, tess_type_pool_t *);
void              tess_type_pool_deinit(mos_allocator_t *, tess_type_pool_t *);

// -- pool operations --

int tess_type_pool_move_back(mos_allocator_t *, tess_type_pool_t *, tess_type_t *, size_t *);

#endif
