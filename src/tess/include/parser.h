#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "tokenizer.h"

#include "alloc.h"
#include "vector.h"

typedef struct parser parser;

typedef struct parser_error {
    tokenizer_error *tokenizer;
    token           *token;
    tess_error_tag   tag;
} parser_error;

// -- allocation and deallocation --

nodiscard parser *parser_create(allocator *, char const *, size_t) mallocfun;
void              parser_destroy(parser **);

// -- access --

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node **);

int  parser_parse_all(allocator *, parser *, vector *out);

#endif
