#include "token.h"

#include "alloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

//

void token_init(token *tok, token_tag tag) {
    tok->tag  = tag;
    tok->s    = 0;
    tok->file = null;
    tok->line = 0;
    tok->col  = 0;
}

void token_init_v(token *tok, token_tag tag, u8 val) {
    tok->tag  = tag;
    tok->val  = val;
    tok->file = null;
    tok->line = 0;
    tok->col  = 0;
}

int token_init_s(allocator *alloc, token *tok, token_tag tag, char const *s) {

    tok->tag = tag;
    tok->s   = alloc_strdup(alloc, s);
    if (!tok->s) return 1;

    return 0;
}

int token_init_sn(allocator *alloc, token *tok, token_tag tag, char const *s, size_t len) {

    tok->file = null;
    tok->line = 0;
    tok->col  = 0;

    tok->tag  = tag;
    tok->s    = alloc_strndup(alloc, s, len);
    if (!tok->s) return 1;

    return 0;
}

void token_deinit(allocator *alloc, token *tok) {
    switch (tok->tag) {
    case tok_comma:
    case tok_dot:
    case tok_colon:
    case tok_colon_equal:
    case tok_semicolon:
    case tok_ampersand:
    case tok_star:
    case tok_arrow:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_equal_sign:
    case tok_invalid:     break;
    case tok_number:
    case tok_symbol:
    case tok_string:
    case tok_comment:     alloc_free(alloc, tok->s); break;
    }

    alloc_invalidate(tok);
}

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *token_tag_to_string(token_tag tag) {
    static char const *const strings[] = {TESS_TOKEN_TAGS(MOS_TAG_STRING)};
    return strings[tag];
}

char *token_to_string(allocator *alloc, token const *tok) {
    char buf[64];

    switch (tok->tag) {
    case tok_comma:
    case tok_dot:
    case tok_colon:
    case tok_colon_equal:
    case tok_semicolon:
    case tok_ampersand:
    case tok_star:
    case tok_arrow:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_equal_sign:
    case tok_invalid:     sprintf(buf, "(%s)", token_tag_to_string(tok->tag)); break;

    case tok_number:
    case tok_symbol:
    case tok_string:
    case tok_comment:     {
        char *big = alloc_malloc(alloc, strlen(tok->s) + 64);
        if (!big) return big;
        sprintf(big, "(%s \"%s\")", token_tag_to_string(tok->tag), tok->s);
        big = alloc_realloc(alloc, big, strlen(big) + 1);
        return big;
    }
    }

    char *out = alloc_malloc(alloc, strlen(buf) + 1);
    if (!out) return out;
    strcpy(out, buf);
    return out;
}
