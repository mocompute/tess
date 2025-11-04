#ifndef TESS_TOKEN_H
#define TESS_TOKEN_H

#include "alloc.h"
#include "array.h"
#include "types.h"

#ifndef MOS_TAG_NAME
#define MOS_TAG_NAME(name, str) name,
#endif

#define TESS_TOKEN_TAGS(X)                                                                                 \
    X(tok_bang, "bang")                                                                                    \
    X(tok_bang_equal, "bang_equal")                                                                        \
    X(tok_hash_command, "hash_directive")                                                                  \
    X(tok_c_block, "c_block")                                                                              \
    X(tok_comma, "comma")                                                                                  \
    X(tok_dot, "dot")                                                                                      \
    X(tok_colon, "colon")                                                                                  \
    X(tok_colon_equal, "colon_equal")                                                                      \
    X(tok_semicolon, "semicolon")                                                                          \
    X(tok_ampersand, "ampersand")                                                                          \
    X(tok_logical_and, "logical_and")                                                                      \
    X(tok_star, "star")                                                                                    \
    X(tok_arrow, "arrow")                                                                                  \
    X(tok_ellipsis, "ellipsis")                                                                            \
    X(tok_open_round, "open_round")                                                                        \
    X(tok_close_round, "close_round")                                                                      \
    X(tok_open_curly, "open_curly")                                                                        \
    X(tok_close_curly, "close_curly")                                                                      \
    X(tok_open_square, "open_square")                                                                      \
    X(tok_close_square, "close_square")                                                                    \
    X(tok_equal_sign, "equal_sign")                                                                        \
    X(tok_equal_equal, "equal_equal")                                                                      \
    X(tok_invalid, "invalid")                                                                              \
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
    char const *file;
    u32         line;
    u32         col;
    token_tag   tag;

} token;

defarray(token_array, struct token);

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
