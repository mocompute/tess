#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "nodiscard.h"
#include "vector.h"

#include <stdint.h>

#define TESS_TYPE_TAGS(X)                                                                                  \
  X(type_nil, "nil")                                                                                       \
  X(type_bool, "bool")                                                                                     \
  X(type_int, "int")                                                                                       \
  X(type_float, "float")                                                                                   \
  X(type_string, "string")                                                                                 \
  X(type_tuple, "tuple")                                                                                   \
  X(type_arrow, "arrow")                                                                                   \
  X(type_type_var, "type_var")

#define TESS_ENUM(name, str) name,
typedef enum type_tag { TESS_TYPE_TAGS(TESS_ENUM) } type_tag_t;
#undef TESS_ENUM

#define TESS_AST_TAGS(X)                                                                                   \
  X(ast_eof, "eof")                                                                                        \
  X(ast_nil, "nil")                                                                                        \
  X(ast_bool, "bool")                                                                                      \
  X(ast_symbol, "symbol")                                                                                  \
  X(ast_i64, "i64")                                                                                        \
  X(ast_u64, "u64")                                                                                        \
  X(ast_f64, "f64")                                                                                        \
  X(ast_string, "string")                                                                                  \
  X(ast_infix, "infix")                                                                                    \
  X(ast_tuple, "tuple")                                                                                    \
  X(ast_let_in, "let_in")                                                                                  \
  X(ast_let, "let")                                                                                        \
  X(ast_if_then_else, "if_then_else")                                                                      \
  X(ast_lambda_function, "lambda_function")                                                                \
  X(ast_function_declaration, "function_declaration")                                                      \
  X(ast_lambda_declaration, "lambda_declaration")                                                          \
  X(ast_lambda_function_application, "lambda_function_application")                                        \
  X(ast_named_function_application, "named_function_application")

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

// -- ast_node --

#define TESS_AST_OPERATOR_TAGS(X)                                                                          \
  X(ast_op_addition, "+")                                                                                  \
  X(ast_op_subtraction, "-")                                                                               \
  X(ast_op_multiplication, "*")                                                                            \
  X(ast_op_division, "/")                                                                                  \
                                                                                                           \
  /* NB: see is_arithmetic and is_relational */                                                            \
                                                                                                           \
  X(ast_op_less_than, "<")                                                                                 \
  X(ast_op_less_than_equal, "<=")                                                                          \
  X(ast_op_equal, "==")                                                                                    \
  X(ast_op_not_equal, "<>")                                                                                \
  X(ast_op_greater_than_equal, ">=")                                                                       \
  X(ast_op_greater_than, ">")                                                                              \
  X(ast_op_sentinel, NULL)

#define TESS_ENUM(name, str) name,
typedef enum ast_operator { TESS_AST_OPERATOR_TAGS(TESS_ENUM) } ast_operator_t;
#undef TESS_ENUM

typedef struct {
  size_t val;
} ast_node_h;

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
      ast_node_h     left;
      ast_node_h     right;
    } infix;

    struct {
      mos_vector_t parameters;
      ast_node_h   body;
    } lambda_function;

    struct {
      ast_node_h name;
      ast_node_h value;
      ast_node_h body;
    } let_in;

    struct {
      mos_vector_t parameters;
      ast_node_h   name;
    } function_declaration;

    struct {
      mos_vector_t parameters;
    } lambda_declaration;

    struct {
      mos_vector_t parameters;
      ast_node_h   name;
      ast_node_h   body;
    } let;

    struct {
      ast_node_h condition;
      ast_node_h yes;
      ast_node_h no;
    } if_then_else;

    struct {
      mos_vector_t arguments;
      ast_node_h   lambda;
    } lambda_function_application;

    struct {
      mos_vector_t arguments;
      ast_node_h   name;
      bool         specialized;
    } named_function_application;

    struct {
      mos_vector_t elements;
    } tuple;
  };

  ast_tag_t tag;
} ast_node_t;

// -- ast_pool --

typedef struct ast_pool {
  struct mos_vector data; // ast_node_t
} ast_pool_t;

// -- allocation and deallocation --

void          tess_type_init(tess_type_t *, type_tag_t);
void          tess_type_init_type_var(tess_type_t *, uint32_t);
void          tess_type_init_tuple(tess_type_t *);
void          tess_type_init_arrow(tess_type_t *);
void          tess_type_deinit(mos_allocator_t *, tess_type_t *);

ast_pool_t   *ast_pool_alloc(mos_allocator_t *);
void          ast_pool_dealloc(mos_allocator_t *, ast_pool_t **);
nodiscard int ast_pool_init(mos_allocator_t *, ast_pool_t *);
void          ast_pool_deinit(mos_allocator_t *, ast_pool_t *);

void          ast_node_init(ast_node_t *, ast_tag_t);
void          ast_node_deinit(mos_allocator_t *, ast_node_t *);
void          ast_node_replace(mos_allocator_t *, ast_node_t *, ast_tag_t);

// -- pool operations --
//
// [move_back] takes ownership of ast_node(s) and invalidates caller's copy

nodiscard int ast_pool_move_back(mos_allocator_t *, ast_pool_t *, ast_node_t *, ast_node_h *);
ast_node_t   *ast_pool_at(ast_pool_t *, ast_node_h);

// -- utilities --

char const *type_tag_to_string(type_tag_t);
char const *ast_tag_to_string(ast_tag_t);
int         string_to_ast_operator(char const *, ast_operator_t *);
void        ast_vector_init(mos_vector_t *);

#endif
