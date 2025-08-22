#ifndef MOS_SEXP_PARSER_H
#define MOS_SEXP_PARSER_H

#include "alloc.h"
#include "util.h"

#include <stddef.h>

// -- token --

#define MOS_SEXP_TOKEN_TAGS(X)                                                                             \
  X(sexp_tok_open_round, "open-round")                                                                     \
  X(sexp_tok_close_round, "close-round")                                                                   \
  X(sexp_tok_number, "number")                                                                             \
  X(sexp_tok_string, "string")                                                                             \
  X(sexp_tok_symbol, "symbol")                                                                             \
  X(sexp_tok_single_quote, "single-quote")                                                                 \
  X(sexp_tok_comment, "comment")

typedef enum { MOS_SEXP_TOKEN_TAGS(MOS_TAG_NAME) } sexp_token_tag;

typedef struct {
  char          *s;
  sexp_token_tag tag;
} sexp_token;

// -- tokenizer --

typedef struct {
  allocator  *alloc;

  char const *input;
  size_t      input_len;

  char       *buf;
  size_t      pos;

} sexp_tokenizer;

// -- allocation and deallocation --

nodiscard int sexp_tokenizer_init(allocator *, sexp_tokenizer *, char const *, size_t);
void          sexp_tokenizer_deinit(sexp_tokenizer *);

// -- tokenizer --

int sexp_tokenizer_next(sexp_tokenizer *);

// -- parser --

#endif
