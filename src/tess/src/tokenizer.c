#include "tokenizer.h"

#include "alloc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

//

void tess_token_init(tess_token_t *tok, tess_token_tag_t tag) {
  tok->tag = tag;
  tok->s   = 0;
}

void tess_token_init_v(tess_token_t *tok, tess_token_tag_t tag, uint8_t val) {
  tok->tag = tag;
  tok->val = val;
}

int tess_token_init_s(mos_allocator_t *alloc, tess_token_t *tok, tess_token_tag_t tag, char const *s) {

  tok->tag = tag;
  tok->s   = alloc->malloc(strlen(s) + 1);
  if (!tok->s) return 1;
  strcpy(tok->s, s);

  return 0;
}

void tess_token_deinit(mos_allocator_t *alloc, struct tess_token_t *tok) {
  switch (tok->tag) {
  case one_newline:
  case two_newline:
  case comma:
  case semicolon:
  case arrow:
  case open_round:
  case close_round:
  case equal_sign:
  case invalid:
  case newline_indent: break;
  case number:
  case symbol:
  case string:
  case comment:        alloc->free(tok->s); break;
  }

  mos_alloc_invalidate(tok, sizeof *tok);
}

char const *tess_token_tag_to_string(tess_token_tag_t tag) {
#define STRING_ITEM(name, str) [name]          = str,
  static char const *const token_tag_strings[] = {TOKEN_TAG_LIST(STRING_ITEM)};
#undef STRING_ITEM
  return token_tag_strings[tag];
}

char const *tess_tokenizer_error_tag_to_string(tess_tokenizer_error_tag_t tag) {
#define STRING_ITEM(name, str) [name]                    = str,
  static char const *const tokenizer_error_tag_strings[] = {TOKENIZER_ERROR_TAG_LIST(STRING_ITEM)};
#undef STRING_ITEM
  return tokenizer_error_tag_strings[tag];
}

char *tess_token_to_string(mos_allocator_t *alloc, tess_token_t const *tok) {
  char buf[64];

  switch (tok->tag) {
  case one_newline:
  case two_newline:
  case comma:
  case semicolon:
  case arrow:
  case open_round:
  case close_round:
  case equal_sign:
  case invalid:        sprintf(buf, "(%s)", tess_token_tag_to_string(tok->tag)); break;
  case newline_indent: sprintf(buf, "(%s %d)", tess_token_tag_to_string(tok->tag), tok->val); break;

  case number:
  case symbol:
  case string:
  case comment:        {
    char *big = alloc->malloc(strlen(tok->s) + 64);
    if (!big) return 0;
    sprintf(big, "(%s \"%s\")", tess_token_tag_to_string(tok->tag), tok->s);
    big = alloc->realloc(big, strlen(big) + 1);
    return big;
  }
  }

  char *out = alloc->malloc(strlen(buf) + 1);
  if (!out) return 0;
  strcpy(out, buf);
  return out;
}
