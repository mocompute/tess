#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "tokenizer.h"

#include "alloc.h"

typedef struct parser parser_t;

typedef struct parser_error {
  tokenizer_error_t *tokenizer;
  token_t           *token;
  tess_error_tag_t   tag;
} parser_error_t;

// -- allocation and deallocation --

parser_t     *parser_alloc(mos_allocator *);
void          parser_dealloc(mos_allocator *, parser_t **);
nodiscard int parser_init(mos_allocator *, parser_t *, ast_pool_t *, char const *, size_t);
void          parser_deinit(parser_t *);

// -- access --
//
// Error pointers returned to caller are only valid until the next
// call to the parser. Error is only valid if tess_parser_next returns
// non-zero.

parser_error_t const *parser_error(parser_t *);

// -- parser --

int  parser_next(parser_t *);
void parser_result(parser_t *, ast_node_h *);

#endif
