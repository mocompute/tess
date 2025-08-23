#ifndef TESS_TOKEN_H
#define TESS_TOKEN_H

#include "alloc.h"
#include "util.h"

#include <stdint.h>

#define TESS_TOKEN_TAGS(X)                                                                                 \
    X(tok_one_newline, "one_newline")                                                                      \
    X(tok_two_newline, "two_newline")                                                                      \
    X(tok_comma, "comma")                                                                                  \
    X(tok_semicolon, "semicolon")                                                                          \
    X(tok_arrow, "arrow")                                                                                  \
    X(tok_open_round, "open_round")                                                                        \
    X(tok_close_round, "close_round")                                                                      \
    X(tok_equal_sign, "equal_sign")                                                                        \
    X(tok_invalid, "invalid")                                                                              \
    X(tok_newline_indent, "newline_indent")                                                                \
    X(tok_number, "number")                                                                                \
    X(tok_symbol, "symbol")                                                                                \
    X(tok_string, "string")                                                                                \
    X(tok_comment, "comment")

typedef enum token_tag { TESS_TOKEN_TAGS(MOS_TAG_NAME) } token_tag;

typedef struct token {
    union {
        char *s;
        u8    val;
    };
    token_tag tag;
} token;

// -- allocation and deallocation --

void token_init(token *, token_tag);
void token_init_v(token *, token_tag, u8);
int  token_init_s(allocator *, token *, token_tag, char const *);
int  token_init_sn(allocator *, token *, token_tag, char const *, size_t);
void token_deinit(allocator *, token *);

// -- utilities --

char const *token_tag_to_string(token_tag);
char       *token_to_string(allocator *, token const *);

#endif
