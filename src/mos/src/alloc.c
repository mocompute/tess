#include "alloc.h"
#include <stdint.h>

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

void mos_alloc_invalidate(void *p, size_t len) {
#ifndef NDEBUG
  while (len--) {
    if ((intptr_t)p % 2 == 0) *(unsigned char *)p = 0xde;
    else *(unsigned char *)p = 0xad;
    ++p;
  }
#else
  memset(p, 0, len);
#endif
}
