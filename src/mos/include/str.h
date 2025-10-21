#ifndef MOS_STR
#define MOS_STR

#include "alloc.h"
#include "array.h"
#include "types.h"

#define MOS_STR_MAX_SMALL (sizeof(size_t) + sizeof(char *) - 2)

typedef struct {
    size_t len;
    char  *buf;
    // Do not change order of fields: str depends on it.
} span;

typedef struct {
    union {
        span big;
        struct {
            char         buf[MOS_STR_MAX_SMALL + 1]; // room for \0
            unsigned int len : 4;
            unsigned int tag : 4;
            // 1 if small, anything else if allocated (because it's the low bits of big.buf
        } small;
    };
} str;

defarray(str_array, str);
defsized(str_sized, str);

typedef struct {
    int   len; // for use with C lib
    char *buf;
} ispan;

typedef struct {
    size_t      len;
    char const *buf;
} cspan;

// -- allocation and deallocation --

// Literal string: do not deinit or dcat. Not for converting C strings.
// clang-format off
#define S(p) (str){.big = {.buf = p, .len = sizeof(p) - 1}}
// clang-format on

str  str_empty();
str  str_move(str *);

str  str_init_small(char const *);         // fatal exit if string is too large
str  str_init_static(char const *);        // from static c strings: do not deinit!
str  str_init_allocated(char *);           // from caller's allocator
str  str_init_allocated_n(char *, size_t); // from caller's allocator
str  str_init(allocator *, char const *);
str  str_init_n(allocator *, char const *, size_t);
str  str_init_move_n(char **, size_t); // destructive move
str  str_copy(allocator *, str);
str  str_copy_span(allocator *, span);
void str_deinit(allocator *, str *);

str  str_fmt(allocator *, char const *restrict fmt, ...) __attribute__((format(printf, 2, 3)));

// -- operations --

str  str_cat(allocator *, str, str);
str  str_cat_c(allocator *, str, char const *);
str  str_cat_array(allocator *, str_sized);
str  str_cat_3(allocator *, str, str, str);
str  str_cat_4(allocator *, str, str, str, str);
str  str_cat_5(allocator *, str, str, str, str, str);
str  str_cat_6(allocator *, str, str, str, str, str, str);
str *str_dcat(allocator *, str *lhs, str); // 'd' for destructive; overwrites and returns lhs
str *str_dcat_c(allocator *, str *lhs, char const *);
str *str_dcat_array(allocator *, str *, str_sized);
void str_resize(allocator *, str *, size_t);

// -- queries --

size_t      str_len(str);
int         str_is_empty(str);
int         str_cmp(str, str);
int         str_cmp_v(void const *, void const *);
int         str_cmp_c(str, char const *);
int         str_cmp_nc(str, char const *, size_t);
int         str_eq(str, str);
u32         str_hash32(str);
u64         str_hash64(str);
u32         str_hash32_combine(u32, str);
u64         str_hash64_combine(u64, str);

span        str_span(str *);
ispan       str_ispan(str *); // for use with C lib; exits program on int overflow

span        str_slice_len(str *, size_t start, size_t len); // range checked
span        str_slice_left(str *, size_t start);            // from start to the end

char const *str_buf(str const *);
int         str_ilen(str); // for use with C lib; exits program on int overflow

u64         str_array_hash64(u64 seed, str_sized);
int         str_array_cmp(str_sized, str_sized);
int         str_array_contains(str_sized hay, str_sized need);
int         str_array_contains_one(str_sized hay, str need);
void        str_array_set_insert(str_array *, str); // insert if not present

// -- utilities --

int         str_parse_num(str, i64 *, u64 *, f64 *);           // Returns: 0, 1, 2, 3
int         str_parse_cnum(char const *, i64 *, u64 *, f64 *); // Returns: 0, 1, 2, 3

str         str_init_i64(allocator *, i64);
str         str_init_u64(allocator *, u64);
str         str_init_f64(allocator *, f64);

char const *str_cstr(str *);

// -- string builder --

typedef char_array  str_build;

nodiscard str_build str_build_init(allocator *, u32); // init builder with initial size
void                str_build_cat(str_build *, str);
void                str_build_join(str_build *, str, str const *, u32);
void                str_build_join_array(str_build *, str, str_array);
str                 str_build_str(allocator *, str_build); // construct str from copy of array
str                 str_build_finish(str_build *);         // construct str from destr. move (do not deinit)
void                str_build_deinit(str_build);
#endif
