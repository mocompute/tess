#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "str.h"
#include "tokenizer.h"

#include "alloc.h"
#include "type.h"
#include "type_registry.h"

typedef struct parser parser;

typedef struct parser_error {
    tokenizer_error *tokenizer;
    token           *token;
    tl_error_tag     tag;
    char const      *file;
    u32              line;
    u32              col;
} parser_error;

typedef struct {
    tl_type_registry *registry;
    str_sized         files;
} parser_opts;

// -- allocation and deallocation --

nodiscard parser *parser_create(allocator *, parser_opts const *) mallocfun;
void              parser_destroy(parser **);

// -- module symbols pass --

hashmap *parser_take_module_symbols(parser *);
void     parser_set_module_symbols(parser *, hashmap *);

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node **);

int  parser_parse_all(parser *, ast_node_array *out);
int  parser_parse_all_symbols(parser *);
int  parser_parse_all_verbose(parser *, ast_node_array *out);
void parser_report_errors(parser *);
void parser_set_verbose(parser *, int);

// -- utilities --

int is_arithmetic_operator(char const *);
int is_relational_operator(char const *);
int is_logical_operator(char const *);
int is_index_operator(char const *);
int is_struct_access_operator(char const *);
int is_dot_operator(char const *);
int is_bitwise_operator(char const *);

str mangle_str_for_arity(allocator *, str, u8);

#endif
