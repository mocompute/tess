#include "sexp.h"

#include <assert.h>
#include <stdio.h>

int test_sexp_assert(void) {
  assert(sizeof(void *) == 8);
  assert(sizeof(size_t) == 8);
  assert(sizeof(sexp) == 8);
  return 0;
}

#define T(name)                                                                                            \
  this_error = name();                                                                                     \
  if (this_error) {                                                                                        \
    fprintf(stderr, "FAILED: %s\n", #name);                                                                \
    error += this_error;                                                                                   \
  }

int main(void) {
  int error      = 0;
  int this_error = 0;

  T(test_sexp_assert);

  return error;
}
