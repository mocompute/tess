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

parser       *parser_alloc(allocator *);
parser       *parser_create(allocator *, ast_pool *, char const *, size_t);
void          parser_dealloc(allocator *, parser **);
void          parser_destroy(allocator *, parser **);
nodiscard int parser_init(allocator *, parser *, ast_pool *, char const *, size_t);
void          parser_deinit(parser *);

// -- access --
//
// Error pointers returned to caller are only valid until the next
// call to the parser. Error is only valid if tess_parser_next returns
// non-zero.

parser_error const *parser_get_error(parser *);

// -- parser --

int  parser_next(parser *);
void parser_result(parser *, ast_node_h *);

int  parser_parse_all(allocator *, parser *, vec_t *out);

#endif
