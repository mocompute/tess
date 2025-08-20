#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "nodiscard.h"
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
typedef enum type_tag { TESS_TYPE_TAGS(TESS_ENUM) } type_tag_t;
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
typedef enum ast_tag { TESS_AST_TAGS(TESS_ENUM) } ast_tag_t;
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
  type_tag_t tag;
} tess_type_t;

// -- tess_ast_node --

#define TESS_AST_OPERATOR_TAGS(X)                                                                          \
  X(tess_ast_op_addition, "+")                                                                             \
  X(tess_ast_op_subtraction, "-")                                                                          \
  X(tess_ast_op_multiplication, "*")                                                                       \
  X(tess_ast_op_division, "/")                                                                             \
                                                                                                           \
  /* NB: see is_arithmetic and is_relational */                                                            \
                                                                                                           \
  X(tess_ast_op_less_than, "<")                                                                            \
  X(tess_ast_op_less_than_equal, "<=")                                                                     \
  X(tess_ast_op_equal, "==")                                                                               \
  X(tess_ast_op_not_equal, "<>")                                                                           \
  X(tess_ast_op_greater_than_equal, ">=")                                                                  \
  X(tess_ast_op_greater_than, ">")                                                                         \
  X(tess_ast_op_sentinel, NULL)

#define TESS_ENUM(name, str) name,
typedef enum ast_operator { TESS_AST_OPERATOR_TAGS(TESS_ENUM) } ast_operator_t;
#undef TESS_ENUM

typedef struct ast_node {
  union {

    struct {
      char *name;
    } symbol;

    struct {
      bool val;
    } bool_;

    struct {
      int64_t val;
    } i64;

    struct {
      uint64_t val;
    } u64;

    struct {
      double val;
    } f64;

    struct {
      ast_operator_t op;
      size_t         left;
      size_t         right;
    } infix;

    struct {
      mos_vector_t parameters;
      size_t       body;
    } lambda_function;

    struct {
      size_t name;
      size_t value;
      size_t body;
    } let_in;

    struct {
      mos_vector_t parameters;
      size_t       name;
    } function_declaration;

    struct {
      mos_vector_t parameters;
    } lambda_declaration;

    struct {
      mos_vector_t parameters;
      size_t       name;
      size_t       body;
    } let;

    struct {
      size_t condition;
      size_t yes;
      size_t no;
    } if_then_else;

    struct {
      mos_vector_t arguments;
      size_t       lambda;
    } lambda_function_application;

    struct {
      mos_vector_t arguments;
      size_t       name;
      bool         specialized;
    } named_function_application;

    struct {
      mos_vector_t elements;
    } tuple;
  };

  ast_tag_t tag;
} ast_node_t;

// -- tess_ast_pool --

typedef struct ast_pool {
  struct mos_vector data; // tess_ast_node_t
} ast_pool_t;

// -- allocation and deallocation --

// tess_type

void tess_type_init(tess_type_t *, type_tag_t);
void tess_type_init_type_var(tess_type_t *, uint32_t);
void tess_type_init_tuple(tess_type_t *);
void tess_type_init_arrow(tess_type_t *);
void tess_type_deinit(mos_allocator_t *, tess_type_t *);

// tess_ast_pool

ast_pool_t   *ast_pool_alloc(mos_allocator_t *);
void          ast_pool_dealloc(mos_allocator_t *, ast_pool_t **);
nodiscard int ast_pool_init(mos_allocator_t *, ast_pool_t *);
void          ast_pool_deinit(mos_allocator_t *, ast_pool_t *);

// tess_ast_node

void ast_node_init(ast_node_t *, ast_tag_t);
void ast_node_deinit(mos_allocator_t *, ast_node_t *);
void ast_node_replace(mos_allocator_t *, ast_node_t *, ast_tag_t);

// -- pool operations --
//
// [move_back] takes ownership of ast_node(s) and invalidates caller's copy

nodiscard int ast_pool_move_back(mos_allocator_t *, ast_pool_t *, ast_node_t *, size_t *);
ast_node_t   *ast_pool_at(ast_pool_t *, size_t);

// -- utilities --

char const *type_tag_to_string(type_tag_t);
char const *ast_tag_to_string(ast_tag_t);
int         string_to_ast_operator(char const *, ast_operator_t *);

#endif
