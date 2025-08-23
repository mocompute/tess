#ifndef MOS_STRING_H
#define MOS_STRING_H

#include "alloc.h"

#include <stdbool.h>
#include <stdint.h>

#define MOS_STRING_MAX_SMALL_LEN 14

typedef struct {
    union {
        struct {
            char *buf;
            char  pad[8];
        } allocated;
        struct {
            char data[MOS_STRING_MAX_SMALL_LEN + 1];
            char tag;
        } small;
    };
} string_t;

// -- allocation and deallocation --

nodiscard int mos_string_init(allocator *, string_t *, char const *);
nodiscard int mos_string_init_n(allocator *, string_t *, char const *, size_t);
void          mos_string_init_empty(string_t *);
void          mos_string_deinit(allocator *, string_t *);
nodiscard int mos_string_replace(allocator *, string_t *, char const *);
void          mos_string_move(string_t *, string_t *);

// -- access --

char const *mos_string_str(string_t const *);

// -- utilities --

bool mos_string_is_allocated(string_t const *);

int  mos_string_parse_number(char const *, i64 *, u64 *, f64 *);
// Returns: 0, 1, 2, 3

#endif
