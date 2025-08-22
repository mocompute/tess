#include "sexp_parser.h"
#include "alloc.h"

int sexp_tokenizer_init(allocator *alloc, sexp_tokenizer *t, char const *input, size_t len) {
  alloc_zero(t);

  t->alloc     = alloc;
  t->input     = input;
  t->input_len = len;

  t->buf       = alloc->malloc(alloc, 1024);

  if (NULL == t->buf) return 1;
  return 0;
}

void sexp_tokenizer_deinit(sexp_tokenizer *t) {
  t->alloc->free(t->alloc, t->buf);
  alloc_invalidate(t);
}
