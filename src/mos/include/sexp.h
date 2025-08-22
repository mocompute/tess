#ifndef MOS_SEXP_H
#define MOS_SEXP_H

#include "util.h"

#include <stdint.h>

// -- sexp --

#define MOS_SEXP_TAGS(X)                                                                                   \
  X(sexp_nil, "nil")                                                                                       \
  X(sexp_boxed, "[boxed]")                                                                                 \
  X(sexp_signed, "signed-integer")                                                                         \
  X(sexp_signed_neg, "signed-integer-neg")                                                                 \
  X(sexp_COUNT, "COUNT")

typedef enum { MOS_SEXP_TAGS(MOS_TAG_NAME) } sexp_tag;

typedef struct sexp {

  // assumes 64-bit platform
  uint64_t val : 62;
  sexp_tag tag : 2;

} sexp;

#endif
