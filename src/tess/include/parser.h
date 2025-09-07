#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "tokenizer.h"

#include "alloc.h"

typedef struct parser parser;

typedef struct parser_error {
    tokenizer_error *tokenizer;
    token           *token;
    tess_error_tag   tag;
    char const      *file;
    u32              line;
} parser_error;

// -- allocation and deallocation --

nodiscard parser *parser_create(allocator *, char_cslice) mallocfun;
void              parser_destroy(parser **);

// -- access --

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node **);

int  parser_parse_all(parser *, ast_node_array *out);
int  parser_parse_all_verbose(parser *, ast_node_array *out);
void parser_report_errors(parser *);

#endif
