#include "token.h"

#include "alloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

//

void token_init(token_t *tok, token_tag_t tag) {
  tok->tag = tag;
  tok->s   = 0;
}

void token_init_v(token_t *tok, token_tag_t tag, uint8_t val) {
  tok->tag = tag;
  tok->val = val;
}

int token_init_s(mos_allocator_t *alloc, token_t *tok, token_tag_t tag, char const *s) {

  tok->tag = tag;
  tok->s   = alloc->malloc(strlen(s) + 1);
  if (!tok->s) return 1;
  strcpy(tok->s, s);

  return 0;
}

int token_init_sn(mos_allocator_t *alloc, token_t *tok, token_tag_t tag, char const *s, size_t len) {

  tok->tag = tag;
  tok->s   = alloc->malloc(len + 1);
  if (!tok->s) return 1;
  memcpy(tok->s, s, len);
  tok->s[len] = 0;

  return 0;
}

void token_deinit(mos_allocator_t *alloc, token_t *tok) {
  switch (tok->tag) {
  case tok_one_newline:
  case tok_two_newline:
  case tok_comma:
  case tok_semicolon:
  case tok_arrow:
  case tok_open_round:
  case tok_close_round:
  case tok_equal_sign:
  case tok_invalid:
  case tok_newline_indent: break;
  case tok_number:
  case tok_symbol:
  case tok_string:
  case tok_comment:        alloc->free(tok->s); break;
  }

  mos_alloc_invalidate(tok, sizeof *tok);
}

char const *token_tag_to_string(token_tag_t tag) {
#define STRING_ITEM(name, str) [name] = str,
  static char const *const strings[]  = {TESS_TOKEN_TAGS(STRING_ITEM)};
#undef STRING_ITEM
  return strings[tag];
}

char *token_to_string(mos_allocator_t *alloc, token_t const *tok) {
  char buf[64];

  switch (tok->tag) {
  case tok_one_newline:
  case tok_two_newline:
  case tok_comma:
  case tok_semicolon:
  case tok_arrow:
  case tok_open_round:
  case tok_close_round:
  case tok_equal_sign:
  case tok_invalid:        sprintf(buf, "(%s)", token_tag_to_string(tok->tag)); break;
  case tok_newline_indent: sprintf(buf, "(%s %d)", token_tag_to_string(tok->tag), tok->val); break;

  case tok_number:
  case tok_symbol:
  case tok_string:
  case tok_comment:        {
    char *big = alloc->malloc(strlen(tok->s) + 64);
    if (!big) return 0;
    sprintf(big, "(%s \"%s\")", token_tag_to_string(tok->tag), tok->s);
    big = alloc->realloc(big, strlen(big) + 1);
    return big;
  }
  }

  char *out = alloc->malloc(strlen(buf) + 1);
  if (!out) return 0;
  strcpy(out, buf);
  return out;
}
