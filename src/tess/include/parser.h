#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "nodiscard.h"
#include "tokenizer.h"

#include "alloc.h"

typedef struct parser parser;

typedef struct parser_error {
  tokenizer_error_t *tokenizer;
  token             *token;
  tess_error_tag     tag;
} parser_error;

// -- allocation and deallocation --

parser       *parser_alloc(mos_allocator *);
void          parser_dealloc(mos_allocator *, parser **);
nodiscard int parser_init(mos_allocator *, parser *, ast_pool *, char const *, size_t);
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

#endif
