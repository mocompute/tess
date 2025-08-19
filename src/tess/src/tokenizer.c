#include "tokenizer.h"
#include "alloc.h"

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
