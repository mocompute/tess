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

#define TESS_AST_TAGS(X)                                                                                   \
  X(tess_ast_eof, "eof")                                                                                   \
  X(tess_ast_nil, "nil")                                                                                   \
  X(tess_ast_bool, "bool")                                                                                 \
  X(tess_ast_symbol, "symbol")                                                                             \
  X(tess_ast_i64, "i64")                                                                                   \
  X(tess_ast_u64, "u64")                                                                                   \
  X(tess_ast_f64, "f64")                                                                                   \
  X(tess_ast_string, "string")                                                                             \
  X(tess_ast_infix, "infix")                                                                               \
  X(tess_ast_tuple, "tuple")                                                                               \
  X(tess_ast_let_in, "let_in")                                                                             \
  X(tess_ast_let, "let")                                                                                   \
  X(tess_ast_if_then_else, "if_then_else")                                                                 \
  X(tess_ast_lambda_function, "lambda_function")                                                           \
  X(tess_ast_function_declaration, "function_declaration")                                                 \
  X(tess_ast_lambda_declaration, "lambda_declaration")                                                     \
  X(tess_ast_lambda_function_application, "lambda_function_application")                                   \
  X(tess_ast_named_function_application, "named_function_application")

#define TESS_ENUM(name, str) name,
typedef enum tess_ast_tag { TESS_AST_TAGS(TESS_ENUM) } tess_ast_tag_t;
#undef TESS_ENUM

struct arrow_type {
  size_t left;
  size_t right;
};

typedef struct tess_type {
  union {
    struct mos_vector tuple;
    struct arrow_type arrow;
    uint32_t          val;
  };
  tess_type_tag_t tag;
} tess_type_t;

typedef struct tess_type_pool {
  struct mos_vector data; // tess_type_t
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
void              tess_type_pool_dealloc(mos_allocator_t *, tess_type_pool_t *);
void              tess_type_pool_init(mos_allocator_t *, tess_type_pool_t *);
void              tess_type_pool_deinit(mos_allocator_t *, tess_type_pool_t *);

// -- pool operations --
//
// move_back() takes ownership of type and invalidates caller's copy

int tess_type_pool_move_back(mos_allocator_t *, tess_type_pool_t *, tess_type_t *, size_t *);

// -- utilities --

char const *tess_type_tag_to_string(tess_type_tag_t);
char const *tess_ast_tag_to_string(tess_ast_tag_t);

#endif
