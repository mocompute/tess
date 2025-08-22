#include "mos_string.h"
#include "alloc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int mos_string_init(allocator *alloc, string_t *s, char const *src) {
  assert(8 == sizeof(char *));

  size_t len = strlen(src);
  if (len == 0) {
    s->buf = (char *)1;
    return 0;
  }

  if (len < 8) {
    memcpy(s->small.data, src, len);
    s->small.status = (uint8_t)(len << 4) | 1;
    return 0;
  }

  s->buf = alloc_strndup(alloc, src, len);
  if (NULL == s->buf) return 1;
  if ((intptr_t)s->buf % 2 == 1) return 1; // odd pointer address

  return 0;
}

void mos_string_deinit(allocator *alloc, string_t *s) {
  if ((intptr_t)s->buf % 2 == 0) alloc->free(alloc, s->buf);
  alloc_invalidate(s);
}

int mos_string_replace(allocator *alloc, string_t *s, char const *src) {
  if ((intptr_t)s->buf % 2 == 0) alloc->free(alloc, s->buf);
  return mos_string_init(alloc, s, src);
}

bool mos_string_is_allocated(string_t *s) {
  return ((intptr_t)s->buf % 2 == 0);
}
