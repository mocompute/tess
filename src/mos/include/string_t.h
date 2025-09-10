#ifndef MOS_STRING_H
#define MOS_STRING_H

#include "alloc.h"
#include "types.h"

#define MOS_STRING_MAX_SMALL_LEN 14
#define MOS_STRING_MAX_LEN       UINT32_MAX

// TODO: get rid of this typedef
typedef struct {
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

string_t string_t_init(allocator *, char const *);
string_t string_t_init_n(allocator *, char const *, size_t);
string_t string_t_init_empty();
void     string_t_deinit(allocator *, string_t *);
void     string_t_replace(allocator *, string_t *, char const *);
void     string_t_move(string_t *, string_t *);
void     string_t_copy(allocator *, string_t *, string_t const *);

// -- access --

char const *string_t_str(string_t const *);
u32         string_t_size(string_t const *);
int         string_t_empty(string_t const *);
u32         string_t_hash(string_t const *);

// -- utilities --

int string_t_cmp_c(string_t const *, char const *);
int string_t_parse_number(char const *, i64 *, u64 *, f64 *);
// Returns: 0, 1, 2, 3

int string_t_is_allocated(string_t const *);
u32 string_t_hash32(string_t const *);

#endif
