#include "sexp.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int test_sexp_assert(void) {
  assert(sizeof(void *) == 8);
  assert(sizeof(size_t) == 8);
  assert(sizeof(sexp) == 8);

  assert(offsetof(sexp_boxed, symbol.name) == offsetof(sexp_boxed, string.name));

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

  printf("INT64_MAX             = %lli\n", INT64_MAX);
  printf("INT64_MIN             = %lli\n", INT64_MIN);
  printf("INT64_MAX / 2         = %lli\n", INT64_MAX / 2);
  printf("INT64_MIN / 2         = %lli\n", INT64_MIN / 2);
  printf("INT64_MAX >> 1        = %lli\n", INT64_MAX >> 1);
  printf("INT64_MIN >> 1        = %lli\n", INT64_MIN >> 1);
  printf("INT64_MAX/2 << 1 >> 1 = %lli\n", ((INT64_MAX >> 1) << 1) >> 1);
  printf("INT64_MIN/2 << 1 >> 1 = %lli\n", ((INT64_MIN >> 1) << 1) >> 1);

  T(test_sexp_assert);

  return error;
}
