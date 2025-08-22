#ifndef MOS_SEXP_H
#define MOS_SEXP_H

#include "alloc.h"
#include "mos_string.h"
#include "nodiscard.h"
#include "util.h"
#include "vector.h"

#include <stdint.h>

#define SEXP_MAX_UNBOXED_INT (INT64_MAX >> 1)
#define SEXP_MIN_UNBOXED_INT (INT64_MIN >> 1)

// -- sexp --

#define MOS_SEXP_BOXED_TAGS(X)                                                                             \
  X(sexp_boxed_i64, "[signed]")                                                                            \
  X(sexp_boxed_u64, "[unsigned]")                                                                          \
  X(sexp_boxed_f64, "[double]")                                                                            \
  X(sexp_boxed_symbol, "[symbol]")                                                                         \
  X(sexp_boxed_string, "[string]")                                                                         \
  X(sexp_boxed_list, "[list]")                                                                             \
  X(sexp_boxed_COUNT, "COUNT")

typedef enum { MOS_SEXP_BOXED_TAGS(MOS_TAG_NAME) } sexp_boxed_tag;

typedef struct {
  union {
    struct {
      int64_t val;
    } i64;
    struct {
      uint64_t val;
    } u64;
    struct {
      double val;
    } f64;
    struct {
      string_t name;
    } symbol;
    struct {
      string_t name;
    } string;
    struct {
      vec_t list;
    } list;
  };

  sexp_boxed_tag tag;
} sexp_boxed;

typedef struct {

  union {
    sexp_boxed *ptr;
    int64_t     integer;
  };

} sexp;

// -- allocation and deallocation --

void          sexp_init_unboxed(sexp *, int64_t);
nodiscard int sexp_init_boxed(allocator *, sexp *);
nodiscard int sexp_init_i64(allocator *, sexp *, int64_t);
nodiscard int sexp_init_u64(allocator *, sexp *, uint64_t);
nodiscard int sexp_init_f64(allocator *, sexp *, double);

void          sexp_boxed_init_empty(sexp_boxed *);
void          sexp_boxed_init_move_string(sexp_boxed *, sexp_boxed_tag, string_t *);
void          sexp_boxed_init_move_list(sexp_boxed *, vec_t *);
void          sexp_boxed_deinit(allocator *, sexp_boxed *);

// -- access --

int64_t     sexp_unboxed_get(sexp);
sexp_boxed *sexp_boxed_get(sexp);

#endif
