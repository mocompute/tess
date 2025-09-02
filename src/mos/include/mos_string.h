#ifndef MOS_STRING_H
#define MOS_STRING_H

#include "alloc.h"
#include "types.h"

#include <stdbool.h>

#define MOS_STRING_MAX_SMALL_LEN 14
#define MOS_STRING_MAX_LEN       UINT32_MAX

// TODO: get rid of this typedef
typedef struct string {
    union {
        struct {
            char *buf;
            u32   size;
            byte  pad[4];
        } allocated;
        struct {
            char data[MOS_STRING_MAX_SMALL_LEN + 1];
            byte tag;
        } small;
    };
} string_t;

// -- allocation and deallocation --

struct string mos_string_init(allocator *, char const *);
struct string mos_string_init_n(allocator *, char const *, size_t);
struct string mos_string_init_empty();
void          mos_string_deinit(allocator *, struct string *);
void          mos_string_replace(allocator *, struct string *, char const *);
void          mos_string_move(struct string *, struct string *);
void          mos_string_copy(allocator *, struct string *, struct string const *);

// -- access --

char const *mos_string_str(struct string const *);
u32         mos_string_size(struct string const *);
bool        mos_string_empty(struct string const *);
u32         mos_string_hash(struct string const *);

// -- utilities --

int mos_string_parse_number(char const *, i64 *, u64 *, f64 *);
// Returns: 0, 1, 2, 3

bool mos_string_is_allocated(struct string const *);
u32  mos_string_hash32(struct string const *);

#endif
