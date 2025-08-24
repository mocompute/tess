#include "mos_string.h"

#include "alloc.h"
#include "hash.h"
#include "types.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mos_string_init_empty(string_t *s) {
    alloc_zero(s);
    s->small.tag = 1;
}

int mos_string_init_n(allocator *alloc, string_t *s, char const *src, size_t max) {
    assert(8 == sizeof(char *));
    assert(16 == sizeof(string_t));

    size_t len = strlen(src);
    if (len > max) len = max;

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
    if (null == s->allocated.buf) return 1;

    return 0;
}

int mos_string_init(allocator *alloc, string_t *s, char const *src) {
    return mos_string_init_n(alloc, s, src, SIZE_MAX);
}

void mos_string_deinit(allocator *alloc, string_t *s) {
    if (mos_string_is_allocated(s)) alloc_free(alloc, s->allocated.buf);
    alloc_invalidate(s);
}

int mos_string_replace(allocator *alloc, string_t *s, char const *src) {
    if (mos_string_is_allocated(s)) alloc_free(alloc, s->allocated.buf);
    return mos_string_init(alloc, s, src);
}

void mos_string_move(string_t *dst, string_t *src) {
    alloc_copy(dst, src);
    alloc_zero(src);
}

int mos_string_copy(allocator *alloc, string_t *dst, string_t const *src) {
    alloc_copy(dst, src);

    if (mos_string_is_allocated(dst)) {
        dst->allocated.buf = alloc_strdup(alloc, dst->allocated.buf);
        if (!dst) return 1;
    }

    return 0;
}

char const *mos_string_str(string_t const *s) {
    if (mos_string_is_allocated(s)) return s->allocated.buf;
    return &s->small.data[0];
}

bool mos_string_is_allocated(string_t const *s) {
    return s->small.tag == 1;
}

int mos_string_parse_number(char const *in, i64 *out_i64, u64 *out_u64, f64 *out_f64) {
    // Returns: 0, 1, 2, 3

    errno                 = 0;
    ptrdiff_t const len   = (ptrdiff_t)strlen(in);

    char           *p_end = 0;
    long long int   i     = strtoll(in, &p_end, 10);
    if (p_end - in == len && !errno) {
        *out_i64 = i;
        return 1;

    } else {

        errno                = 0;
        p_end                = 0;
        unsigned long long u = strtoull(in, &p_end, 10);
        if (p_end - in == len && !errno) {
            *out_u64 = u;
            return 2;

        } else {

            errno = 0;
            p_end = 0;
            f64 d = strtod(in, &p_end);
            if (p_end - in == len && !errno) {
                *out_f64 = d;
                return 3;
            }
        }
    }

    return 0;
}

u32 mos_string_hash32(string_t const *self) {
    char const *str = mos_string_str(self);
    return hash32((u8 const *)str, strlen(str));
}
