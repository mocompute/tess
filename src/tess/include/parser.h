#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "ast.h"
#include "tokenizer.h"

#include "alloc.h"

typedef struct tess_parser tess_parser_t;

typedef struct tess_parser_error {
  tess_tokenizer_error_t *tokenizer;
  tess_token_t           *token;
  tess_error_tag_t        tag;
} tess_parser_error_t;

// -- allocation and deallocation --

tess_parser_t *tess_parser_alloc(mos_allocator_t *);
void           tess_parser_dealloc(mos_allocator_t *, tess_parser_t **);
int            tess_parser_init(mos_allocator_t *, tess_parser_t *, char const *, size_t);
void           tess_parser_deinit(tess_parser_t *);

// -- access --
//
// Error pointers returned to caller only live until the next call to
// the parser. Error is only valid if tess_parser_next returns
// non-zero.

tess_parser_error_t const *tess_parser_error(tess_parser_t *);

// -- parser --

int  tess_parser_next(tess_parser_t *);
void tess_parser_result(tess_parser_t *, tess_ast_node_t **, size_t *);

#endif
