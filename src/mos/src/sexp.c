#include "sexp.h"
#include "alloc.h"
#include "mos_string.h"
#include "vector.h"

#include <assert.h>

void sexp_init_unboxed(sexp *self, int64_t val) {
  assert(val <= SEXP_MAX_UNBOXED_INT && val >= SEXP_MIN_UNBOXED_INT);

  self->integer = val << 1;
}

int64_t sexp_unboxed_get(sexp const *self) {
  return self->integer >> 1;
}

void sexp_boxed_init_move_string(sexp_boxed *self, sexp_boxed_tag tag, string_t *src) {
  self->tag = tag;
  mos_string_move(&self->symbol.name, src);
}

void sexp_boxed_init_move_list(sexp_boxed *self, vec_t *src) {
  self->tag = sexp_boxed_list;
  vec_move(&self->list.list, src);
}

void sexp_boxed_deinit(allocator *alloc, sexp_boxed *self) {
  switch (self->tag) {
  case sexp_boxed_i64:
  case sexp_boxed_u64:
  case sexp_boxed_f64:
  case sexp_boxed_COUNT:  break;
  case sexp_boxed_symbol:
  case sexp_boxed_string: mos_string_deinit(alloc, &self->symbol.name); break;
  case sexp_boxed_list:   vec_deinit(alloc, &self->list.list); break;
  }

  alloc_invalidate(self);
}
