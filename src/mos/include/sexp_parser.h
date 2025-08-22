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

#define MOS_SEXP_TOKENIZER_ERR_TAGS(X)                                                                     \
  X(sexp_tok_err_eof, "eof")                                                                               \
  X(sexp_tok_err_oom, "oom")                                                                               \
  X(sexp_tok_err_invalid_token, "invalid_token")                                                           \
  X(sexp_tok_err_unexpected_error, "unexpected_error")

typedef enum { MOS_SEXP_TOKENIZER_ERR_TAGS(MOS_TAG_NAME) } sexp_tokenizer_err_tag;

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
  size_t      buf_len;
  size_t      pos;

} sexp_tokenizer;

// -- allocation and deallocation --

nodiscard int sexp_tokenizer_init(allocator *, sexp_tokenizer *, char const *, size_t);
void          sexp_tokenizer_deinit(sexp_tokenizer *);

void          sexp_token_init(sexp_token *, sexp_token_tag);
nodiscard int sexp_token_init_str(allocator *, sexp_token *, sexp_token_tag, char const *, size_t);
void          sexp_token_deinit(allocator *, sexp_token *);

// -- tokenizer --

int sexp_tokenizer_next(sexp_tokenizer *, sexp_token *, sexp_tokenizer_err_tag *, size_t *err_pos);

// -- parser --

#endif
