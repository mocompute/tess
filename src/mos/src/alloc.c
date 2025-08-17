#include "alloc.h"

// use LSAN's allocators if present
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#else
#include <stdlib.h>
#endif
#else
#include <stdlib.h>
#endif

mos_allocator_t *mos_alloc_default_allocator() {
  static mos_allocator_t allocator = {&malloc, &calloc, &realloc, &free};
  return &allocator;
}
