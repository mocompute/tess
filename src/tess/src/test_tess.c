#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int test_tess(void) {
  int error = 0;
  return error;
}

#define T(name)                                                                                            \
  this_error = name();                                                                                     \
  if (this_error) {                                                                                        \
    fprintf(stderr, "FAILED: %s\n", #name);                                                                \
    error += this_error;                                                                                   \
  }

int main(void) {
  int          error      = 0;
  int          this_error = 0;

  unsigned int seed       = (unsigned int)time(0);

  fprintf(stderr, "Seed = %u\n\n", seed);
  srand(seed);

  T(test_tess);

  return error;
}
