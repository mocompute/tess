#include "token.h"

#include "alloc.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

//

void token_init(token *tok, token_tag tag) {
  tok->tag = tag;
  tok->s   = 0;
}

void token_init_v(token *tok, token_tag tag, uint8_t val) {
  tok->tag = tag;
  tok->val = val;
}

int token_init_s(mos_allocator *alloc, token *tok, token_tag tag, char const *s) {

  tok->tag = tag;
  tok->s   = alloc->malloc(alloc, strlen(s) + 1);
  if (!tok->s) return 1;
  strcpy(tok->s, s);

  return 0;
}

int token_init_sn(mos_allocator *alloc, token *tok, token_tag tag, char const *s, size_t len) {

  tok->tag = tag;
  tok->s   = alloc->malloc(alloc, len + 1);
  if (!tok->s) return 1;
  memcpy(tok->s, s, len);
  tok->s[len] = 0;

  return 0;
}

void token_deinit(mos_allocator *alloc, token *tok) {
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
  case tok_comment:        alloc->free(alloc, tok->s); break;
  }

  mos_alloc_invalidate(tok, sizeof *tok);
}

char const *token_tag_to_string(token_tag tag) {
  static char const *const strings[] = {TESS_TOKEN_TAGS(MOS_TAG_STRING)};
  return strings[tag];
}

char *token_to_string(mos_allocator *alloc, token const *tok) {
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
    char *big = alloc->malloc(alloc, strlen(tok->s) + 64);
    if (!big) return 0;
    sprintf(big, "(%s \"%s\")", token_tag_to_string(tok->tag), tok->s);
    big = alloc->realloc(alloc, big, strlen(big) + 1);
    return big;
  }
  }

  char *out = alloc->malloc(alloc, strlen(buf) + 1);
  if (!out) return 0;
  strcpy(out, buf);
  return out;
}
