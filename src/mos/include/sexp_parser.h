#ifndef MOS_SEXP_PARSER_H
#define MOS_SEXP_PARSER_H

#include "alloc.h"
#include "mos_string.h"
#include "sexp.h"

#include <stddef.h>

// -- token --

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define MOS_SEXP_TOKEN_TAGS(X)                                                                             \
    X(sexp_tok_open_round, "open-round")                                                                   \
    X(sexp_tok_close_round, "close-round")                                                                 \
    X(sexp_tok_number, "number")                                                                           \
    X(sexp_tok_string, "string")                                                                           \
    X(sexp_tok_symbol, "symbol")                                                                           \
    X(sexp_tok_comment, "comment")

typedef enum { MOS_SEXP_TOKEN_TAGS(MOS_TAG_NAME) } sexp_token_tag;

#define MOS_SEXP_ERR_TAGS(X)                                                                               \
    X(sexp_tok_err_eof, "eof")                                                                             \
    X(sexp_tok_err_oom, "oom")                                                                             \
    X(sexp_tok_err_invalid_token, "invalid_token")                                                         \
    X(sexp_tok_err_unexpected_error, "unexpected_error")                                                   \
    X(sexp_tok_err_close_round, "close_round")                                                             \
    X(sexp_tok_err_number, "number")

typedef enum { MOS_SEXP_ERR_TAGS(MOS_TAG_NAME) } sexp_err_tag;

typedef struct {
    string_t       s;
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

// -- parser --

typedef struct {
    allocator      *alloc;
    sexp_tokenizer *tokenizer;
} sexp_parser;

// -- allocation and deallocation --

nodiscard int sexp_tokenizer_init(allocator *, sexp_tokenizer *, char const *, size_t);
void          sexp_tokenizer_deinit(sexp_tokenizer *);

nodiscard int sexp_parser_init(allocator *, sexp_parser *, char const *, size_t);
void          sexp_parser_deinit(sexp_parser *);

void          sexp_token_init(sexp_token *, sexp_token_tag);
nodiscard int sexp_token_init_str(allocator *, sexp_token *, sexp_token_tag, char const *, size_t);
void          sexp_token_deinit(allocator *, sexp_token *);

// -- tokenizer operations --

nodiscard int sexp_tokenizer_next(sexp_tokenizer *, sexp_token *, sexp_err_tag *, size_t *);

// -- parser operations --

nodiscard int sexp_parser_next(sexp_parser *, sexp *, sexp_err_tag *, size_t *);

// -- utilities --

char const *sexp_token_tag_to_string(sexp_token_tag);
char const *sexp_err_tag_to_string(sexp_err_tag);

#endif
