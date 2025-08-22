#include "mos_string.h"

#include "alloc.h"
#include "dbg.h"

#include <stdio.h>
#include <time.h>

int test_string(void) {
  int        error = 0;

  allocator *alloc = alloc_default_allocator();

  int        tries = 10000;
  for (int i = 0; i < tries; ++i) {
    int   n    = rand() % (16 * 1024);

    char *data = calloc((size_t)n + 1, 1);

    for (int j = 0; j < n; ++j) {
      data[j] = (char)rand();
    }
    data[n] = '\0';

    string_t s;
    if (mos_string_init(alloc, &s, data)) {
      free(data);
      return error + 1;
    }

    error += (strlen(data) > MOS_STRING_MAX_SMALL_LEN) == mos_string_is_allocated(&s) ? 0 : 1;
    error += 0 == strcmp(mos_string_str(&s), data) ? 0 : 1;

    if (error) {
      dbg("test_string: %s\n", data);
      mos_string_deinit(alloc, &s);
      free(data);
      return error;
    }

    mos_string_deinit(alloc, &s);
    free(data);
  }

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

  T(test_string);

  return error;
}
