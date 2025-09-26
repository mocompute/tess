#ifndef MOS_STR
#define MOS_STR

#include "alloc.h"
#include "array.h"
#include "types.h"

#define MOS_STR_MAX_SMALL (sizeof(size_t) + sizeof(char *) - 1)

typedef struct {
    size_t len;
    char  *buf;
    // Do not change order of fields: str depends on it.
} span;

typedef struct {
    union {
        span big;
        struct {
            char         buf[MOS_STR_MAX_SMALL];
            unsigned int len : 4;
            unsigned int tag : 4;
            // 1 if small, anything else if allocated (because it's the low bits of allocated.buf
        } small;
    };
} str;

typedef struct {
    array_header;
    str *v;
} str_array;

typedef struct {
    array_sized;
    str *v;
} str_sized;

// -- allocation and deallocation --

str  str_empty();
str  str_move(str *);

int  str_init_small(str *, char const *); // return nonzero if input too large
str  str_init_static(char const *);       // from static c strings: do not deinit!
str  str_init(allocator *, char const *);
str  str_init_n(allocator *, char const *, size_t);
str  str_copy(allocator *, str);
void str_deinit(allocator *, str *);

// -- operations --

str  str_cat(allocator *, str, str);
str *str_dcat(allocator *, str *lhs, str); // 'd' for destructive; returns lhs
void str_resize(allocator *, str *, size_t);

// -- queries --

size_t str_len(str);
int    str_is_empty(str);
int    str_cmp(str, str);
int    str_cmp_c(str, char const *);
int    str_eq(str, str);
u32    str_hash32(str);
u64    str_hash64(str);

span   str_span(str *);
char  *str_buf(str *);
int    str_ilen(str *); // for use with C lib; exits program on int overflow

int    str_array_cmp(str_sized, str_sized);
int    str_array_contains(str_sized hay, str_sized need);

// -- utilities --

int str_parse_num(str, i64 *, u64 *, f64 *);           // Returns: 0, 1, 2, 3
int str_parse_cnum(char const *, i64 *, u64 *, f64 *); // Returns: 0, 1, 2, 3

#endif
