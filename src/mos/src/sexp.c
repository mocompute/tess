#include "sexp.h"
#include "alloc.h"
#include "mos_string.h"
#include "vector.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

void sexp_init_unboxed(sexp *self, int64_t val) {
  assert(val <= SEXP_MAX_UNBOXED_INT && val >= SEXP_MIN_UNBOXED_INT);
  self->integer = (val << 1) | 1;
}

int64_t sexp_unboxed_get(sexp self) {
  return self.integer >> 1;
}

sexp_box *sexp_box_get(sexp self) {
  return self.ptr;
}

void sexp_box_init_empty(sexp_box *self) {
  alloc_zero(self);
  self->tag = sexp_box_list;
  vec_init_empty(&self->list.list, sizeof(sexp));
}

void sexp_box_init_move_string(sexp_box *self, sexp_box_tag tag, string_t *src) {
  self->tag = tag;
  mos_string_move(&self->symbol.name, src);
}

void sexp_box_init_move_list(sexp_box *self, vec_t *src) {
  self->tag = sexp_box_list;
  vec_move(&self->list.list, src);
}

int sexp_init_boxed(allocator *alloc, sexp *self) {
  alloc_zero(self);
  self->ptr = alloc->malloc(alloc, sizeof(sexp_box));
  if (NULL == self->ptr) return 1;

  sexp_box_init_empty(self->ptr);

  return 0;
}

int sexp_init_i64(allocator *alloc, sexp *self, int64_t val) {
  if (val >= SEXP_MIN_UNBOXED_INT && val <= SEXP_MAX_UNBOXED_INT) {
    sexp_init_unboxed(self, val);
  } else {
    if (sexp_init_boxed(alloc, self)) return 1;
    sexp_box *box = sexp_box_get(*self);
    box->tag      = sexp_box_i64;
    box->i64.val  = val;
  }
  return 0;
}

int sexp_init_u64(allocator *alloc, sexp *self, uint64_t val) {
  if (sexp_init_boxed(alloc, self)) return 1;
  sexp_box *box = sexp_box_get(*self);
  box->tag      = sexp_box_u64;
  box->u64.val  = val;
  return 0;
}

int sexp_init_f64(allocator *alloc, sexp *self, double val) {
  if (sexp_init_boxed(alloc, self)) return 1;
  sexp_box *box = sexp_box_get(*self);
  box->tag      = sexp_box_f64;
  box->f64.val  = val;
  return 0;
}

void sexp_box_deinit(allocator *alloc, sexp_box *self) {
  switch (self->tag) {
  case sexp_box_i64:
  case sexp_box_u64:
  case sexp_box_f64:    break;
  case sexp_box_symbol:
  case sexp_box_string: mos_string_deinit(alloc, &self->symbol.name); break;
  case sexp_box_list:   vec_deinit(alloc, &self->list.list); break;
  }

  alloc_invalidate(self);
}

bool sexp_is_boxed(sexp self) {
  return (self.integer & 1) == 0;
}

static int print_node(sexp const *node, char *restrict buf, int const sz_, char const *restrict literal) {
  if (sz_ < 0) return -1;
  size_t const sz = (size_t)sz_;

  if (NULL != literal) {
    return snprintf(buf, sz, "%s", literal);
  }

  int offset = 0;

#define do_print_init() int res = 0;

#define do_print_node(NODE)                                                                                \
  do {                                                                                                     \
    if (buf) res = print_node(NODE, buf + offset, sz_ - offset, NULL);                                     \
    else res = print_node(NODE, NULL, 0, NULL);                                                            \
    if (res < 0) return res;                                                                               \
    offset += res;                                                                                         \
  } while (0)

#define do_print_literal(LITERAL)                                                                          \
  do {                                                                                                     \
    if (buf) res = print_node(NULL, buf + offset, sz_ - offset, LITERAL);                                  \
    else res = print_node(NULL, NULL, 0, LITERAL);                                                         \
    if (res < 0) return res;                                                                               \
    offset += res;                                                                                         \
  } while (0)

#define do_print_list(FIELD)                                                                               \
  do {                                                                                                     \
    size_t      count = vec_size(&FIELD);                                                                  \
    sexp const *it    = vec_cbegin(&FIELD);                                                                \
    while (count--) {                                                                                      \
                                                                                                           \
      do_print_node(it);                                                                                   \
      if (count) do_print_literal(" ");                                                                    \
                                                                                                           \
      ++it;                                                                                                \
    }                                                                                                      \
  } while (0)

  if (!sexp_is_boxed(*node)) return snprintf(buf, sz, "%" PRId64, sexp_unboxed_get(*node));
  sexp_box *box = sexp_box_get(*node);

  switch (box->tag) {
  case sexp_box_i64:    return snprintf(buf, sz, "%" PRId64, box->i64.val);
  case sexp_box_u64:    return snprintf(buf, sz, "%" PRIu64, box->u64.val);
  case sexp_box_f64:    return snprintf(buf, sz, "%f", box->f64.val);
  case sexp_box_symbol: return snprintf(buf, sz, "%s", mos_string_str(&box->symbol.name));
  case sexp_box_string: return snprintf(buf, sz, "\"%s\"", mos_string_str(&box->symbol.name));

  case sexp_box_list:   {
    do_print_init();
    do_print_literal("(");
    do_print_list(box->list.list);
    do_print_literal(")");
  } break;
  }

  return offset;

#undef do_print_node
#undef do_print_literal
}

int sexp_to_string_buf(sexp const *node, char *buf, size_t sz_) {
  if (sz_ > INT_MAX) return 1;
  int sz  = (int)sz_;

  int res = print_node(node, buf, sz, NULL);

  // check error conditions from snprintf
  if (res < 0 || res > sz) return 1;
  return 0;
}

char *sexp_to_string(allocator *alloc, sexp node) {
  int sz = print_node(&node, NULL, 0, NULL);
  if (sz < 0) return NULL;

  char *out = alloc->malloc(alloc, (size_t)sz + 1);
  if (NULL == out) return out;

  print_node(&node, out, sz + 1, NULL);
  return out;
}
