#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "array.h"
#include "ast.h"
#include "nodiscard.h"
#include "str.h"
#include "tokenizer.h"

#include "alloc.h"

typedef struct parser parser;

typedef struct parser_error {
    tokenizer_error *tokenizer;
    token           *token;
    tl_error_tag     tag;
    char const      *file;
    u32              line;
    u32              col;
} parser_error;

// -- allocation and deallocation --

nodiscard parser *parser_create(allocator *, char_csized, str_sized) mallocfun;
void              parser_destroy(parser **);

// -- access --

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node **);

int  parser_parse_all(parser *, ast_node_array *out);
int  parser_parse_all_verbose(parser *, ast_node_array *out);
void parser_report_errors(parser *);

// -- utilities --

int is_arithmetic_operator(char const *);
int is_relational_operator(char const *);
int is_logical_operator(char const *);
int is_index_operator(char const *);
int is_struct_access_operator(char const *);
int is_dot_operator(char const *);

#endif
