#include "alloc.h"

#include <sanitizer/lsan_interface.h>
/* #include <stdlib.h> */

mos_allocator_t *mos_alloc_default_allocator() {
  static mos_allocator_t allocator = {&malloc, &calloc, &realloc, &free};
  return &allocator;
}
