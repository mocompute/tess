#include "str.h"
#include "alloc.h"
#include "hash.h"

#include <errno.h>
#include <string.h>

#define STR_SMALL 1

str str_empty() {
    return (str){.small.tag = 1, .small.len = 0};
}

str str_move(str *orig) {
    str out         = *orig;
    orig->small.len = 0;
    orig->small.tag = STR_SMALL;
    return out;
}

static void init_small(str *out, char const *in, size_t len) {
    if (len > MOS_STR_MAX_SMALL) fatal("out of range");

    out->small.tag = STR_SMALL;
    out->small.len = len;
    memcpy(&out->small.buf[0], in, len);
}

static int is_small(str self) {
    return self.small.tag == STR_SMALL;
}

int str_init_small(str *out, char const *in) {
    size_t len = strlen(in);
    if (len > MOS_STR_MAX_SMALL) return 1;
    init_small(out, in, len);
    return 0;
}

str str_init_static(char const *in) {
    // danger zone: do not deinit or modify this
    return (str){.big = {.buf = (char *)in, .len = strlen(in)}};
}

str str_init(allocator *alloc, char const *in) {
    return str_init_n(alloc, in, strlen(in));
}

str str_init_n(allocator *alloc, char const *in, size_t len) {
    if (len <= MOS_STR_MAX_SMALL) {
        str out;
        init_small(&out, in, len);
        return out;
    }

    str out = {.big = {.len = len, .buf = alloc_malloc(alloc, len)}};
    memcpy(&out.big.buf[0], in, len);
    return out;
}

str str_copy(allocator *alloc, str in) {
    size_t len = str_len(in);
    if (len <= MOS_STR_MAX_SMALL) {
        return in;
    }

    str out = {.big = {.len = len, .buf = alloc_malloc(alloc, len)}};
    memcpy(&out.big.buf[0], &in.big.buf[0], len);
    return out;
}

void str_deinit(allocator *alloc, str *self) {
    if (is_small(*self)) {
        self->small.len = 0;
        return;
    }

    alloc_free(alloc, self->big.buf);
    *self = str_empty();
}

str str_cat(allocator *alloc, str left, str right) {

    span   left_span  = str_span(&left);
    span   right_span = str_span(&right);
    size_t len        = left_span.len + right_span.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len)}};
    span out_span = str_span(&out);

    memcpy(&out_span.buf[0], &left_span.buf[0], left_span.len);
    memcpy(&out_span.buf[left_span.len], &right_span.buf[0], right_span.len);
    return out;
}

str *str_dcat(allocator *alloc, str *left, str right) {
    span   right_span = str_span(&right);
    size_t left_len   = str_len(*left);
    size_t len        = left_len + right_span.len;

    str_resize(alloc, left, len);
    span left_span = str_span(left);
    memcpy(&left_span.buf[left_len], &right_span.buf[0], right_span.len);
    return left;
}

void str_resize(allocator *alloc, str *self, size_t len) {
    span old = str_span(self);

    if (len <= MOS_STR_MAX_SMALL) {
        if (!is_small(*self)) {
            self->small.tag = STR_SMALL;
            self->small.len = len;
            memcpy(&self->small.buf[0], old.buf, old.len);
            alloc_free(alloc, old.buf);
        }
        self->small.len = len;
        return;
    }

    if (is_small(*self)) {
        char buf[MOS_STR_MAX_SMALL];
        memcpy(buf, old.buf, old.len);
        self->big.buf = alloc_malloc(alloc, len);
        memcpy(&self->big.buf[0], buf, old.len);
    }
    self->big.len = len;
}

size_t str_len(str self) {
    if (is_small(self)) return self.small.len;
    return self.big.len;
}

int str_is_empty(str self) {
    return 0 == str_len(self);
}

int str_cmp(str lhs, str rhs) {
    span left = str_span(&lhs), right = str_span(&rhs);
    if (left.len < right.len) return -1;
    if (left.len > right.len) return 1;

    if (!left.len) return 0;
    return memcmp(&left.buf[0], &right.buf[0], left.len);
}

int str_cmp_c(str lhs, char const *rhs) {
    return str_cmp(lhs, (str){.big = {.buf = (char *)rhs, .len = strlen(rhs)}});
}

int str_eq(str lhs, str rhs) {
    return 0 == str_cmp(lhs, rhs);
}

u32 str_hash32(str self) {
    span s = str_span(&self);
    return hash32(&s.buf[0], s.len);
}

u64 str_hash64(str self) {
    span s = str_span(&self);
    return hash64(&s.buf[0], s.len);
}

span str_span(str *self) {
    if (is_small(*self)) return (span){.len = self->small.len, .buf = self->small.buf};
    else return (span){.len = self->big.len, .buf = self->big.buf};
}

char *str_buf(str *self) {
    if (is_small(*self)) return self->small.buf;
    else return self->big.buf;
}

int str_ilen(str *self) {
    size_t len;
    if (is_small(*self)) len = self->small.len;
    else len = self->big.len;
    if (len > INT_MAX) fatal("overflow");
    return (int)len;
}

int str_array_cmp(str_sized lhs, str_sized rhs) {

    if (lhs.size < rhs.size) return -1;
    if (lhs.size > rhs.size) return 1;
    if (!lhs.size) return 0;

    forall(i, lhs) {
        int res;
        if ((res = str_cmp(lhs.v[i], rhs.v[i]))) return res;
    }
    return 0;
}

int str_array_contains(str_sized hay, str_sized need) {
    forall(i, need) {
        forall(j, hay) if (0 == str_cmp(need.v[i], hay.v[j])) goto found;
        return 0; // finished inner loop without finding
    found:;
    }
    return 1; // finished outer loop without error
}

int str_parse_cnum(char const *buf, i64 *out_i64, u64 *out_u64, f64 *out_f64) {
    // Returns: 0: error, 1: i64, 2: u64, 3: f64.
    // Unlike C functions, input string must not have garbage after valid number.

    size_t      len     = strlen(buf);
    char const *end     = &buf[len];

    errno               = 0;

    char         *p_end = 0;
    long long int i     = strtoll(buf, &p_end, 10);
    if (p_end == end && !errno) {
        *out_i64 = i;
        return 1;

    } else {

        errno                = 0;
        p_end                = 0;
        unsigned long long u = strtoull(buf, &p_end, 10);
        if (p_end == end && !errno) {
            *out_u64 = u;
            return 2;

        } else {

            errno    = 0;
            p_end    = 0;
            double d = strtod(buf, &p_end);
            if (p_end == end && !errno) {
                *out_f64 = d;
                return 3;
            }
        }
    }

    return 0;
}

int str_parse_num(str self, i64 *out_i64, u64 *out_u64, f64 *out_f64) {
    // Returns: 0: error, 1: i64, 2: u64, 3: f64.
    // Unlike C functions, input string must not have garbage after valid number.

    char buf[64];
    span s = str_span(&self);
    if (s.len > 63) s.len = 63;
    memcpy(&buf[0], &s.buf[0], s.len);
    buf[s.len] = '\0';

    return str_parse_cnum(buf, out_i64, out_u64, out_f64);
}
