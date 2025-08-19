#ifndef TESS_PARSER_H
#define TESS_PARSER_H

#include "tokenizer.h"

#include "alloc.h"

typedef struct tess_parser tess_parser_t;

typedef struct tess_parser_error {
  tess_tokenizer_error_t *tokenizer;
  tess_token_t           *token;
  tess_error_tag_t        tag;
} tess_parser_error_t;

// -- allocation and deallocation --
//
// Error pointers returned to caller only live until the next call to
// the parser.

tess_parser_t *tess_parser_alloc(mos_allocator_t *);
void           tess_parser_dealloc(mos_allocator_t *, tess_parser_t *);
int            tess_parser_init(mos_allocator_t *, tess_parser_t *, char const *, size_t);
void           tess_parser_deinit(mos_allocator_t *, tess_parser_t *);

// -- parser --

int tess_parser_next(tess_parser_t *, tess_parser_error_t const **);

#endif
