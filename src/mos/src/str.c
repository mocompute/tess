#include "str.h"
#include "alloc.h"
#include "array.h"
#include "hash.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STR_SMALL 1

str str_empty() {
    return (str){.small.tag = STR_SMALL, .small.len = 0};
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

str str_init_small(char const *in) {
    size_t len = strlen(in);
    if (len > MOS_STR_MAX_SMALL) fatal("out of range");
    str out;
    init_small(&out, in, len);
    return out;
}

str str_init_static(char const *in) {
    // danger zone: do not deinit or modify the str returned from this function
    return (str){.big = {.buf = (char *)in, .len = strlen(in)}};
}

str str_init_allocated(char *in) {
    return str_init_allocated_n(in, strlen(in));
}

str str_init_allocated_n(char *in, size_t n) {
    return (str){.big = {.buf = in, .len = n}};
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

    // always allocate space for a terminating null to support str_cstr
    str out = {.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    memcpy(&out.big.buf[0], in, len);
    return out;
}

str str_init_move_n(char **move_from, size_t len) {
    if (len <= MOS_STR_MAX_SMALL) {
        str out;
        init_small(&out, *move_from, len);
        // don't set move_from to null; caller must check it and free
        return out;
    }

    str out    = str_init_allocated_n(*move_from, len);
    *move_from = null;
    return out;
}

str str_copy(allocator *alloc, str in) {
    span s = str_span(&in);
    if (s.len <= MOS_STR_MAX_SMALL) {
        str out;
        init_small(&out, s.buf, s.len);
        return out;
    }

    str out = (str){.big = {.len = s.len, .buf = alloc_malloc(alloc, s.len + 1)}};
    memcpy(&out.big.buf[0], &s.buf[0], s.len);
    return out;
}

str str_copy_span(allocator *alloc, span in) {
    str in_str = (str){.big = {.buf = in.buf, .len = in.len}};
    return str_copy(alloc, in_str);
}

void str_deinit(allocator *alloc, str *self) {
    if (is_small(*self)) {
        self->small.len = 0;
        return;
    }

    alloc_free(alloc, self->big.buf);
    *self = str_empty();
}

str str_fmt(allocator *alloc, char const *restrict fmt, ...) {
    va_list args;
    va_list args2;

    //

    va_start(args, fmt);
    va_copy(args2, args);
    int len = vsnprintf(null, 0, fmt, args) + 1;
    va_end(args);
    if (len < 0) fatal("str_fmt error");

    // len includes the null terminator written by vsnprintf.
    // That null byte also serves as the extra byte for str_cstr.
    str out = {.big = {.len = len - 1, .buf = alloc_malloc(alloc, len)}};

    // args2 already has saved state from va_copy - use it directly
    vsnprintf(out.big.buf, len, fmt, args2);
    va_end(args2);

    return out;
}

str str_cat(allocator *alloc, str left, str right) {
    span   left_span  = str_span(&left);
    span   right_span = str_span(&right);
    size_t len        = left_span.len + right_span.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    memcpy(&out_span.buf[0], &left_span.buf[0], left_span.len);
    memcpy(&out_span.buf[left_span.len], &right_span.buf[0], right_span.len);
    return out;
}

str str_cat_c(allocator *alloc, str left, char const *right) {
    return str_cat(alloc, left, str_init_static(right));
}

str str_cat_3(allocator *alloc, str one, str two, str three) {
    span   s1  = str_span(&one);
    span   s2  = str_span(&two);
    span   s3  = str_span(&three);
    size_t len = s1.len + s2.len + s3.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    memcpy(out_span.buf, s1.buf, s1.len);
    memcpy(out_span.buf + s1.len, s2.buf, s2.len);
    memcpy(out_span.buf + s1.len + s2.len, s3.buf, s3.len);
    return out;
}

str str_cat_4(allocator *alloc, str one, str two, str three, str four) {
    span   s1  = str_span(&one);
    span   s2  = str_span(&two);
    span   s3  = str_span(&three);
    span   s4  = str_span(&four);
    size_t len = s1.len + s2.len + s3.len + s4.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    // clang-format off
    size_t off = 0;
    memcpy(out_span.buf + off, s1.buf, s1.len); off += s1.len;
    memcpy(out_span.buf + off, s2.buf, s2.len); off += s2.len;
    memcpy(out_span.buf + off, s3.buf, s3.len); off += s3.len;
    memcpy(out_span.buf + off, s4.buf, s4.len);
    return out;
    // clang-format on
}

str str_cat_5(allocator *alloc, str one, str two, str three, str four, str five) {
    span   s1  = str_span(&one);
    span   s2  = str_span(&two);
    span   s3  = str_span(&three);
    span   s4  = str_span(&four);
    span   s5  = str_span(&five);
    size_t len = s1.len + s2.len + s3.len + s4.len + s5.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    // clang-format off
    size_t off = 0;
    memcpy(out_span.buf + off, s1.buf, s1.len); off += s1.len;
    memcpy(out_span.buf + off, s2.buf, s2.len); off += s2.len;
    memcpy(out_span.buf + off, s3.buf, s3.len); off += s3.len;
    memcpy(out_span.buf + off, s4.buf, s4.len); off += s4.len;
    memcpy(out_span.buf + off, s5.buf, s5.len);
    return out;
    // clang-format on
}

str str_cat_6(allocator *alloc, str one, str two, str three, str four, str five, str six) {
    span   s1  = str_span(&one);
    span   s2  = str_span(&two);
    span   s3  = str_span(&three);
    span   s4  = str_span(&four);
    span   s5  = str_span(&five);
    span   s6  = str_span(&six);
    size_t len = s1.len + s2.len + s3.len + s4.len + s5.len + s6.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    // clang-format off
    size_t off = 0;
    memcpy(out_span.buf + off, s1.buf, s1.len); off += s1.len;
    memcpy(out_span.buf + off, s2.buf, s2.len); off += s2.len;
    memcpy(out_span.buf + off, s3.buf, s3.len); off += s3.len;
    memcpy(out_span.buf + off, s4.buf, s4.len); off += s4.len;
    memcpy(out_span.buf + off, s5.buf, s5.len); off += s5.len;
    memcpy(out_span.buf + off, s6.buf, s6.len);
    return out;
    // clang-format on
}

str str_cat_array(allocator *alloc, str_sized arr) {
    if (!arr.size) return str_empty();
    if (arr.size == 1) return str_copy(alloc, arr.v[0]);

    size_t total = 0;
    forall(i, arr) total += str_len(arr.v[i]);

    if (!total) return str_empty();

    str out;
    if (total <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = total, .tag = STR_SMALL}};
    else out = (str){.big = {.len = total, .buf = alloc_malloc(alloc, total + 1)}};
    span   out_span = str_span(&out);

    size_t off      = 0;
    forall(i, arr) {
        span s = str_span(&arr.v[i]);
        memcpy(out_span.buf + off, s.buf, s.len);
        off += s.len;
    }
    return out;
}

str *str_dcat(allocator *alloc, str *left, str right) {
    span   right_span = str_span(&right);
    size_t orig_len   = str_len(*left);
    size_t new_len    = orig_len + right_span.len;

    str_resize(alloc, left, new_len);
    span left_span = str_span(left);
    memcpy(&left_span.buf[orig_len], &right_span.buf[0], right_span.len);
    return left;
}

str *str_dcat_c(allocator *alloc, str *left, char const *right) {
    return str_dcat(alloc, left, str_init_static(right));
}

str *str_dcat_array(allocator *alloc, str *lhs, str_sized arr) {
    forall(i, arr) str_dcat(alloc, lhs, arr.v[i]);
    return lhs;
}

void str_resize(allocator *alloc, str *self, size_t len) {
    span old = str_span(self);

    if (len <= MOS_STR_MAX_SMALL) {
        if (!is_small(*self)) {
            self->small.tag = STR_SMALL;
            self->small.len = len;
            memcpy(&self->small.buf[0], old.buf, len);
            alloc_free(alloc, old.buf);
        }
        self->small.len = len;
        return;
    }

    if (is_small(*self)) {
        char buf[MOS_STR_MAX_SMALL];
        assert(old.len <= MOS_STR_MAX_SMALL);
        memcpy(buf, old.buf, old.len);
        self->big.buf = alloc_malloc(alloc, len + 1);
        memcpy(&self->big.buf[0], buf, old.len);
        self->big.len = len;
        return;
    }

    self->big.buf = alloc_realloc(alloc, self->big.buf, len + 1);
    self->big.len = len;
}

str str_replace_char(allocator *alloc, str s, char find, char replace) {
    span   src = str_span(&s);
    size_t len = src.len;

    str    out;
    if (len <= MOS_STR_MAX_SMALL) out = (str){.small = {.len = len, .tag = STR_SMALL}};
    else out = (str){.big = {.len = len, .buf = alloc_malloc(alloc, len + 1)}};
    span out_span = str_span(&out);

    for (size_t i = 0; i < len; ++i) out_span.buf[i] = src.buf[i] == find ? replace : src.buf[i];
    return out;
}

str str_replace_char_str(allocator *alloc, str s, char find, str replace) {
    span      src     = str_span(&s);
    span      rep     = str_span(&replace);
    str_build builder = str_build_init(alloc, (u32)src.len);

    for (size_t i = 0; i < src.len; ++i) {
        if (src.buf[i] == find) {
            array_push_many(builder, rep.buf, rep.len);
        } else {
            array_push(builder, src.buf[i]);
        }
    }

    return str_build_finish(&builder);
}

size_t str_len(str self) {
    if (is_small(self)) return self.small.len;
    return self.big.len;
}

int str_is_empty(str self) {
    return 0 == str_len(self);
}

int str_cmp(str lhs, str rhs) {
    span   left = str_span(&lhs), right = str_span(&rhs);
    size_t max_len = min(left.len, right.len);
    int    res     = 0;
    if (max_len && (res = memcmp(left.buf, right.buf, max_len))) return res;
    if (left.len < right.len) return -1;
    if (left.len > right.len) return 1;
    return 0;
}

int str_cmp_v(void const *lhs, void const *rhs) {
    return str_cmp(*(str const *)lhs, *(str const *)rhs);
}

int str_cmp_c(str lhs, char const *rhs) {
    return str_cmp(lhs, (str){.big = {.buf = (char *)rhs, .len = strlen(rhs)}});
}

int str_cmp_nc(str lhs, char const *rhs, size_t max) {
    if (!max) return 0;
    if (!rhs) return 1;
    span   left    = str_span(&lhs);
    size_t rhs_len = strlen(rhs);
    if (max > left.len) return 1; // Note: acts like starts_with
    if (max > rhs_len) max = rhs_len;

    return memcmp(left.buf, rhs, max);
}

int str_starts_with(str data, str prefix) {
    size_t data_len   = str_len(data);
    size_t prefix_len = str_len(prefix);
    if (data_len < prefix_len) return 0;

    char const *data_buf   = str_buf(&data);
    char const *prefix_buf = str_buf(&prefix);
    return 0 == memcmp(data_buf, prefix_buf, prefix_len);
}

int str_ends_with(str data, str suffix) {
    size_t data_len   = str_len(data);
    size_t suffix_len = str_len(suffix);
    if (data_len < suffix_len) return 0;

    char const *data_buf   = str_buf(&data);
    char const *suffix_buf = str_buf(&suffix);
    size_t      index      = data_len - suffix_len;
    return 0 == memcmp(data_buf + index, suffix_buf, suffix_len);
}

int str_contains(str haystack, str needle) {
    size_t hlen = str_len(haystack);
    size_t nlen = str_len(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    char const *hbuf = str_buf(&haystack);
    char const *nbuf = str_buf(&needle);
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (0 == memcmp(hbuf + i, nbuf, nlen)) return 1;
    }
    return 0;
}

int str_contains_char(str s, char c) {
    span sp = str_span(&s);
    return memchr(sp.buf, c, sp.len) != null;
}

int str_prefix_char(allocator *alloc, str s, char c, str *prefix) {
    span  sp = str_span(&s);
    char *p  = memchr(sp.buf, c, sp.len);
    if (!p) return 0;
    *prefix = str_init_n(alloc, sp.buf, (size_t)(p - sp.buf));
    return 1;
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

u32 str_hash32_combine(u32 hash, str self) {
    u32 h = str_hash32(self);
    return hash32_combine(hash, &h, sizeof h);
}

u64 str_hash64_combine(u64 hash, str self) {
    u64 h = str_hash64(self);
    return hash64_combine(hash, &h, sizeof h);
}
u64 str_hash64_combine_sized(u64 hash, str_sized arr) {
    forall(i, arr) hash = str_hash64_combine(hash, arr.v[i]);
    return hash;
}

span str_span(str *self) {
    if (is_small(*self)) return (span){.len = self->small.len, .buf = self->small.buf};
    else return (span){.len = self->big.len, .buf = self->big.buf};
}

cspan str_cspan(str const *self) {
    if (is_small(*self)) return (cspan){.len = self->small.len, .buf = self->small.buf};
    else return (cspan){.len = self->big.len, .buf = self->big.buf};
}

span str_slice_len(str *self, size_t start, size_t len) {
    if (!len) return (span){.len = 0, .buf = null};
    span out = str_span(self);
    if (start >= out.len) return (span){.len = 0, .buf = null};
    if (start + len > out.len) len = out.len - start;
    out.buf += start;
    out.len = len;
    return out;
}

span str_slice_left(str *self, size_t start) {
    span out = str_span(self);
    if (start >= out.len) return (span){.len = 0, .buf = null};
    size_t len = out.len - start;
    out.buf += start;
    out.len = len;
    return out;
}

ispan str_ispan(str *self) {
    size_t len;
    if (is_small(*self)) {
        len = self->small.len;
    } else {
        len = self->big.len;
    }
    if (len > INT_MAX) fatal("overflow");
    if (is_small(*self)) return (ispan){.len = (int)len, .buf = self->small.buf};
    else return (ispan){.len = (int)len, .buf = self->big.buf};
}

char const *str_buf(str const *self) {
    if (is_small(*self)) return self->small.buf;
    else return self->big.buf;
}

int str_ilen(str self) {
    size_t len;
    if (is_small(self)) len = self.small.len;
    else len = self.big.len;
    if (len > INT_MAX) fatal("overflow");
    return (int)len;
}

u64 str_array_hash64(u64 seed, str_sized arr) {
    u64 hash = seed;
    forall(i, arr) {
        u64 h = str_hash64(arr.v[i]);
        hash  = hash64_combine(hash, &h, sizeof h);
    }
    return hash;
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

int str_array_contains_one(str_sized hay, str need) {
    return str_array_contains(hay, (str_sized){.size = 1, .v = &need});
}

void str_array_set_insert(str_array *arr, str str) {
    if (!str_array_contains_one((str_sized)sized_all(*arr), str)) array_push(*arr, str);
}

void str_sized_sort(str_sized arr) {
    if (arr.size) qsort(arr.v, arr.size, sizeof arr.v[0], str_cmp_v);
}

int str_parse_cnum(char const *buf, i64 *out_i64, u64 *out_u64, f64 *out_f64) {
    // Returns: 0: error, 1: i64, 2: u64, 3: f64.
    // Unlike C functions, input string must not have garbage after valid number.

    size_t      len = strlen(buf);
    char const *end = &buf[len];

    // Detect binary prefix (0b or 0B)
    int         base        = 0;
    char const *parse_start = buf;
    if (len >= 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        base        = 2;
        parse_start = buf + 2; // Skip "0b" prefix
    }

    errno               = 0;

    char         *p_end = 0;
    long long int i     = strtoll(parse_start, &p_end, base);
    if (p_end == end && !errno) {
        *out_i64 = i;
        return 1;

    } else {

        errno                = 0;
        p_end                = 0;
        unsigned long long u = strtoull(parse_start, &p_end, base);
        if (p_end == end && !errno) {
            *out_u64 = u;
            return 2;

        } else if (base == 0) {
            // Only try float parsing for non-binary numbers
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
    if (s.len > 63) return 0;
    memcpy(&buf[0], &s.buf[0], s.len);
    buf[s.len] = '\0';

    return str_parse_cnum(buf, out_i64, out_u64, out_f64);
}

str str_init_i64(allocator *alloc, i64 val) {
    char buf[32];
    int  len = snprintf(buf, sizeof buf, "%" PRIi64, val);
    return str_init_n(alloc, buf, len);
}

str str_init_u64(allocator *alloc, u64 val) {
    char buf[32];
    int  len = snprintf(buf, sizeof buf, "%" PRIu64, val);
    return str_init_n(alloc, buf, len);
}

str str_init_f64(allocator *alloc, f64 val) {
    char buf[40];
    int  len = snprintf(buf, sizeof buf, "%f", val);
    return str_init_n(alloc, buf, len);
}

void str_parse_words(str in, str_array *out) {
    span        s         = str_span(&in);
    char const *start     = s.buf;
    char const *end       = s.buf + s.len;

    int         in_string = 0;
    char const *pos       = start;

    while (pos < end && isspace(*pos)) pos++;
    start = pos;

    for (; pos < end; ++pos) {
        if (in_string) {
            if (*pos == '"') {
                in_string = 0;
                pos++; // include quotes
                if (start < pos) {
                    str word = str_init_n(out->alloc, start, pos - start);
                    array_push(*out, word);
                }
                while (pos < end && isspace(*pos)) pos++;
                start = pos;
                if (pos < end) --pos;
            }
        } else if (*pos == '"') {
            if (start < pos) {
                str word = str_init_n(out->alloc, start, pos - start);
                array_push(*out, word);
            }
            in_string = 1;
            start     = pos; // include quotes
        } else {
            if (isspace(*pos)) {
                if (start < pos) {
                    str word = str_init_n(out->alloc, start, pos - start);
                    array_push(*out, word);
                }
                while (pos < end && isspace(*pos)) pos++;
                start = pos;
                if (pos < end) --pos;
            }
        }
    }
    if (start < pos) {
        str word = str_init_n(out->alloc, start, pos - start);
        array_push(*out, word);
    }
}

// -- str_build --

nodiscard str_build str_build_init(allocator *alloc, u32 sz) {
    str_build out = {.alloc = alloc};
    array_reserve(out, sz);
    return out;
}

void str_build_deinit(str_build self) {
    array_free(self);
}

void str_build_cat(str_build *self, str str) {
    span s = str_span(&str);
    array_push_many(*self, s.buf, s.len);
}

void str_build_join(str_build *self, str sep, str const *strs, u32 len) {
    span ssep = str_span(&sep);
    for (u32 i = 0; i < len; ++i) {
        cspan sstr = str_cspan(&strs[i]);
        array_push_many(*self, sstr.buf, sstr.len);
        if (i < len - 1) array_push_many(*self, ssep.buf, ssep.len);
    }
}

void str_build_join_array(str_build *self, str sep, str_array strs) {
    str_build_join(self, sep, strs.v, strs.size);
}
void str_build_join_sized(str_build *self, str sep, str_sized strs) {
    str_build_join(self, sep, strs.v, strs.size);
}

str str_build_str(allocator *alloc, str_build self) {
    return str_init_n(alloc, self.v, self.size);
}

str str_build_finish(str_build *p) {
    // grow buffer to make room for \0 for str_cstr
    // clang-format off
    {char _t = '\0'; array_push(*p, _t);}
    p->size--;
    // clang-format on

    str out = str_init_move_n(&p->v, p->size);
    // move will leave p->v unchanged if it's copying into small
    // string storage, so we have to free it.
    if (p->v) array_free(*p);
    alloc_invalidate(p);
    return out;
}

//

char const *str_cstr(str *self) {
    if (is_small(*self)) {
        unsigned len = self->small.len;
        if (self->small.buf[len] != '\0') self->small.buf[len] = '\0';
        return self->small.buf;
    } else {
        size_t len = self->big.len;

        // all buf allocations include an extra byte
        if (self->big.buf[len] != '\0') self->big.buf[len] = '\0';
        return self->big.buf;
    }
}
