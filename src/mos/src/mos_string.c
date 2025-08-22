#include "mos_string.h"

#include "alloc.h"

#include <assert.h>
#include <string.h>

int mos_string_init(allocator *alloc, string_t *s, char const *src) {
  assert(8 == sizeof(char *));
  assert(16 == sizeof(string_t));

  size_t len = strlen(src);
  if (len == 0) {
    s->small.data[0] = '\0';
    s->small.tag     = 0;
    return 0;
  }

  if (len <= MOS_STRING_MAX_SMALL_LEN) {
    s->small.tag = 0;
    memset(s->small.data, 0, MOS_STRING_MAX_SMALL_LEN);
    memcpy(s->small.data, src, len);
    s->small.data[len] = '\0';
    return 0;
  }

  s->small.tag     = 1;
  s->allocated.buf = alloc_strndup(alloc, src, len);
  if (NULL == s->allocated.buf) return 1;

  return 0;
}

void mos_string_deinit(allocator *alloc, string_t *s) {
  if (mos_string_is_allocated(s)) alloc->free(alloc, s->allocated.buf);
  alloc_invalidate(s);
}

int mos_string_replace(allocator *alloc, string_t *s, char const *src) {
  if (mos_string_is_allocated(s)) alloc->free(alloc, s->allocated.buf);
  return mos_string_init(alloc, s, src);
}

char const *mos_string_str(string_t const *s) {
  if (mos_string_is_allocated(s)) return s->allocated.buf;
  return &s->small.data[0];
}

bool mos_string_is_allocated(string_t const *s) {
  return s->small.tag == 1;
}
