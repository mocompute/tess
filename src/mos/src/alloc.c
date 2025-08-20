#include "alloc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// use LSAN's allocators if we can detect they are present
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#else
#include <stdlib.h>
#endif
#else
#include <stdlib.h>
#endif

mos_allocator *mos_alloc_default_allocator() {
  static mos_allocator allocator = {&malloc, &calloc, &realloc, &free};
  return &allocator;
}

char *mos_alloc_strdup(mos_allocator *alloc, char const *src) {
  size_t len = strlen(src);
  char  *out = alloc->malloc(len + 1);
  if (out) {
    memcpy(out, src, len);
    out[len] = '\0';
  }
  return out;
}

char *mos_alloc_strndup(mos_allocator *alloc, char const *src, size_t max) {
  size_t len = strlen(src);
  if (len > max) len = max;
  char *out = alloc->malloc(len + 1);
  if (out) {
    memcpy(out, src, len);
    out[len] = '\0';
  }
  return out;
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
