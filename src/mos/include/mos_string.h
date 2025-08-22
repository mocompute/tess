#ifndef MOS_STRING_H
#define MOS_STRING_H

#include "alloc.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  union {
    struct {
      char    data[7];
      uint8_t status; // len + pointer flag (odd == no pointer)
    } small;

    char *buf;
  };
} string_t;

// -- allocation and deallocation --

nodiscard int mos_string_init(allocator *, string_t *, char const *);
void          mos_string_deinit(allocator *, string_t *);
nodiscard int mos_string_replace(allocator *, string_t *, char const *);

// -- utilities --

bool mos_string_is_allocated(string_t *);

#endif
