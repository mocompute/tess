#include "str.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

static int test_alloc(void) {
    int        error = 0;

    allocator *alloc = leak_detector_create();

    int        tries = 10000;
    for (int i = 0; i < tries; ++i) {
        int   n    = rand() % (16 * 1024);

        char *data = calloc((size_t)n + 1, 1);

        for (int j = 0; j < n; ++j) {
            data[j] = (char)rand();
        }
        data[n] = '\0';

        str s   = str_init(alloc, data);

        error += 0 == str_cmp_c(s, data) ? 0 : 1;

        if (error) {
            dbg("test_string: %s\n", data);
            str_deinit(alloc, &s);
            free(data);
            return error;
        }

        str_deinit(alloc, &s);
        free(data);
    }

    leak_detector_destroy(&alloc);
    return error;
}

static int test_cat(void) {
    int        error  = 0;

    allocator *alloc  = leak_detector_create();
    str        s1     = str_init(alloc, "foo");
    str        s2     = str_init(alloc, "bar");
    str        expect = str_init(alloc, "foobar");

    error += 1 == str_eq(str_cat(alloc, s1, s2), expect) ? 0 : 1;
    error += 0 == str_cmp(s1, str_init(alloc, "foo")) ? 0 : 1;
    error += 0 == str_cmp(s2, str_init(alloc, "bar")) ? 0 : 1;

    leak_detector_destroy(&alloc);
    return error;
}

static int test_dcat(void) {
    int        error  = 0;

    allocator *alloc  = leak_detector_create();
    str        s1     = str_init(alloc, "foo");
    str        s2     = str_init(alloc, "bar");
    str        expect = str_init(alloc, "foobar");

    error += 1 == str_eq(*str_dcat(alloc, &s1, s2), expect) ? 0 : 1;
    error += 0 == str_cmp(s1, str_init(alloc, "foobar")) ? 0 : 1;
    error += 0 == str_cmp(s2, str_init(alloc, "bar")) ? 0 : 1;

    leak_detector_destroy(&alloc);
    return error;
}

static int test_build(void) {
    int        error = 0;

    allocator *alloc = leak_detector_create();
    str_build  build = str_build_init(alloc, 8);

    str        b[]   = {S("hello"), S("and"), S("goodbye")};
    str_build_join(&build, S(" "), b, sizeof b / sizeof b[0]);

    str res = str_build_finish(&build);

    error += str_eq(S("hello and goodbye"), res) ? 0 : 1;

    str_deinit(alloc, &res);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_ends_with(void) {
    int error = 0;

    str data  = S("abcdef");
    error += 1 == str_ends_with(data, S("def")) ? 0 : 1;
    error += 1 == str_ends_with(data, S("abcdef")) ? 0 : 1;

    error += 0 == str_ends_with(data, S("x")) ? 0 : 1;
    error += 0 == str_ends_with(data, S("abcdefg")) ? 0 : 1;

    return error;
}

static int test_contains(void) {
    int error = 0;

    error += 1 == str_contains(S("hello world"), S("world")) ? 0 : 1;
    error += 1 == str_contains(S("hello world"), S("hello")) ? 0 : 1;
    error += 0 == str_contains(S("hello world"), S("xyz")) ? 0 : 1;
    error += 1 == str_contains(S("foo__bar"), S("__")) ? 0 : 1;
    error += 0 == str_contains(S("foo_bar"), S("__")) ? 0 : 1;
    error += 1 == str_contains(S(""), S("")) ? 0 : 1;
    error += 0 == str_contains(S(""), S("a")) ? 0 : 1;
    error += 1 == str_contains(S("a"), S("")) ? 0 : 1;
    error += 1 == str_contains(S("a"), S("a")) ? 0 : 1;

    return error;
}

static int test_contains_char(void) {
    int error = 0;

    error += 1 == str_contains_char(S("Foo.Bar"), '.') ? 0 : 1;
    error += 0 == str_contains_char(S("FooBar"), '.') ? 0 : 1;
    error += 0 == str_contains_char(S(""), '.') ? 0 : 1;
    error += 1 == str_contains_char(S("."), '.') ? 0 : 1;
    error += 1 == str_contains_char(S("a.b.c"), '.') ? 0 : 1;

    return error;
}

static int test_struct_layout(void) {
    // str is a union of 'big' (span) and 'small' (buffer + bitfields).
    // MSVC adds padding before unsigned int bitfields for alignment,
    // which would make sizeof(str) > sizeof(span), breaking the union.
    // Using unsigned char bitfields avoids this padding.
    if (sizeof(str) != sizeof(span)) {
        fprintf(stderr, "sizeof(str)=%zu != sizeof(span)=%zu\n", sizeof(str), sizeof(span));
        return 1;
    }
    return 0;
}

static int test_cmp(void) {
    int error = 0;

    // Two empty strings
    error += 0 == str_cmp(S(""), S("")) ? 0 : 1;

    // Two equal non-empty strings
    error += 0 == str_cmp(S("hello"), S("hello")) ? 0 : 1;

    // Strings where one is prefix of another
    error += str_cmp(S("abc"), S("abcd")) < 0 ? 0 : 1;
    error += str_cmp(S("abcd"), S("abc")) > 0 ? 0 : 1;

    // Strings of equal length with different content
    error += str_cmp(S("abc"), S("abd")) < 0 ? 0 : 1;
    error += str_cmp(S("abd"), S("abc")) > 0 ? 0 : 1;

    // Empty vs non-empty
    error += str_cmp(S(""), S("a")) < 0 ? 0 : 1;
    error += str_cmp(S("a"), S("")) > 0 ? 0 : 1;

    return error;
}

static int test_parse_cnum_binary(void) {
    int error = 0;
    i64 i;
    u64 u;
    f64 f;

    // Basic binary
    error += str_parse_cnum("0b1010", &i, &u, &f) == 1 ? 0 : 1;
    error += i == 10 ? 0 : 1;

    error += str_parse_cnum("0B1111", &i, &u, &f) == 1 ? 0 : 1;
    error += i == 15 ? 0 : 1;

    // Larger binary
    error += str_parse_cnum("0b11110000", &i, &u, &f) == 1 ? 0 : 1;
    error += i == 240 ? 0 : 1;

    // Binary zero
    error += str_parse_cnum("0b0", &i, &u, &f) == 1 ? 0 : 1;
    error += i == 0 ? 0 : 1;

    // All ones byte
    error += str_parse_cnum("0b11111111", &i, &u, &f) == 1 ? 0 : 1;
    error += i == 255 ? 0 : 1;

    return error;
}

static int test_parse_cnum_z_suffix(void) {
    int error = 0;
    i64 i;
    u64 u;
    f64 f;

    // z suffix -> return 4, i64
    error += str_parse_cnum("42z", &i, &u, &f) == 4 ? 0 : 1;
    error += i == 42 ? 0 : 1;

    // zu suffix -> return 5, u64
    error += str_parse_cnum("42zu", &i, &u, &f) == 5 ? 0 : 1;
    error += u == 42 ? 0 : 1;

    // hex with z
    error += str_parse_cnum("0xFFz", &i, &u, &f) == 4 ? 0 : 1;
    error += i == 0xFF ? 0 : 1;

    // hex with zu
    error += str_parse_cnum("0xFFzu", &i, &u, &f) == 5 ? 0 : 1;
    error += u == 0xFF ? 0 : 1;

    // binary with z
    error += str_parse_cnum("0b1010z", &i, &u, &f) == 4 ? 0 : 1;
    error += i == 10 ? 0 : 1;

    // binary with zu
    error += str_parse_cnum("0b1010zu", &i, &u, &f) == 5 ? 0 : 1;
    error += u == 10 ? 0 : 1;

    // uppercase Z
    error += str_parse_cnum("42Z", &i, &u, &f) == 4 ? 0 : 1;
    error += i == 42 ? 0 : 1;

    // uppercase ZU
    error += str_parse_cnum("42ZU", &i, &u, &f) == 5 ? 0 : 1;
    error += u == 42 ? 0 : 1;

    return error;
}

static int test_cat_multi(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // str_cat_3: basic case
    str r3 = str_cat_3(alloc, S("a"), S("b"), S("c"));
    error += str_eq(r3, S("abc")) ? 0 : 1;
    str_deinit(alloc, &r3);

    // str_cat_3: with empty strings
    r3 = str_cat_3(alloc, S(""), S("x"), S(""));
    error += str_eq(r3, S("x")) ? 0 : 1;
    str_deinit(alloc, &r3);

    // str_cat_4: basic case
    str r4 = str_cat_4(alloc, S("a"), S("b"), S("c"), S("d"));
    error += str_eq(r4, S("abcd")) ? 0 : 1;
    str_deinit(alloc, &r4);

    // str_cat_5: basic case
    str r5 = str_cat_5(alloc, S("1"), S("2"), S("3"), S("4"), S("5"));
    error += str_eq(r5, S("12345")) ? 0 : 1;
    str_deinit(alloc, &r5);

    // str_cat_6: basic case
    str r6 = str_cat_6(alloc, S("a"), S("b"), S("c"), S("d"), S("e"), S("f"));
    error += str_eq(r6, S("abcdef")) ? 0 : 1;
    str_deinit(alloc, &r6);

    // str_cat_3: large strings (exceeds small string optimization)
    str big1 = str_init(alloc, "hello_world_");
    str big2 = str_init(alloc, "this_is_a_test_");
    str big3 = str_init(alloc, "string_concatenation");
    str rbig = str_cat_3(alloc, big1, big2, big3);
    error += str_eq(rbig, S("hello_world_this_is_a_test_string_concatenation")) ? 0 : 1;
    str_deinit(alloc, &big1);
    str_deinit(alloc, &big2);
    str_deinit(alloc, &big3);
    str_deinit(alloc, &rbig);

    // str_cat_4: all empty
    r4 = str_cat_4(alloc, S(""), S(""), S(""), S(""));
    error += str_eq(r4, S("")) ? 0 : 1;
    str_deinit(alloc, &r4);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_prefix_char(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();
    str        prefix;

    // Found: basic case
    error += 1 == str_prefix_char(alloc, S("Foo.Bar"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("Foo")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: dot at start
    error += 1 == str_prefix_char(alloc, S(".Bar"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: multiple dots, returns prefix before first
    error += 1 == str_prefix_char(alloc, S("A.B.C"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("A")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Not found
    error += 0 == str_prefix_char(alloc, S("FooBar"), '.', &prefix) ? 0 : 1;

    // Not found: empty string
    error += 0 == str_prefix_char(alloc, S(""), '.', &prefix) ? 0 : 1;

    leak_detector_destroy(&alloc);
    return error;
}

static int test_rprefix_char(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();
    str        prefix;

    // Found: basic case (single dot, same as prefix_char)
    error += 1 == str_rprefix_char(alloc, S("Foo.Bar"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("Foo")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: dot at start
    error += 1 == str_rprefix_char(alloc, S(".Bar"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: multiple dots, returns prefix before last
    error += 1 == str_rprefix_char(alloc, S("A.B.C"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("A.B")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: three dots
    error += 1 == str_rprefix_char(alloc, S("A.B.C.D"), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("A.B.C")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Found: dot at end
    error += 1 == str_rprefix_char(alloc, S("Foo."), '.', &prefix) ? 0 : 1;
    error += str_eq(prefix, S("Foo")) ? 0 : 1;
    str_deinit(alloc, &prefix);

    // Not found
    error += 0 == str_rprefix_char(alloc, S("FooBar"), '.', &prefix) ? 0 : 1;

    // Not found: empty string
    error += 0 == str_rprefix_char(alloc, S(""), '.', &prefix) ? 0 : 1;

    leak_detector_destroy(&alloc);
    return error;
}

static int test_replace_char(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // Basic replacement
    str r = str_replace_char(alloc, S("Foo.Bar.Baz"), '.', '_');
    error += str_eq(r, S("Foo_Bar_Baz")) ? 0 : 1;
    str_deinit(alloc, &r);

    // No occurrences
    r = str_replace_char(alloc, S("hello"), '.', '_');
    error += str_eq(r, S("hello")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Empty string
    r = str_replace_char(alloc, S(""), '.', '_');
    error += str_eq(r, S("")) ? 0 : 1;
    str_deinit(alloc, &r);

    // All characters replaced
    r = str_replace_char(alloc, S("..."), '.', '_');
    error += str_eq(r, S("___")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Large string (exceeds small string optimization)
    r = str_replace_char(alloc, S("a.b.c.d.e.f.g.h.i.j.k.l"), '.', '_');
    error += str_eq(r, S("a_b_c_d_e_f_g_h_i_j_k_l")) ? 0 : 1;
    str_deinit(alloc, &r);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_replace_char_str(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // Basic replacement
    str r = str_replace_char_str(alloc, S("Foo.Bar"), '.', S("__"));
    error += str_eq(r, S("Foo__Bar")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Replace with empty string (deletion)
    r = str_replace_char_str(alloc, S("a.b.c"), '.', S(""));
    error += str_eq(r, S("abc")) ? 0 : 1;
    str_deinit(alloc, &r);

    // No occurrences
    r = str_replace_char_str(alloc, S("hello"), '.', S("__"));
    error += str_eq(r, S("hello")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Empty input string
    r = str_replace_char_str(alloc, S(""), '.', S("__"));
    error += str_eq(r, S("")) ? 0 : 1;
    str_deinit(alloc, &r);

    // All characters replaced
    r = str_replace_char_str(alloc, S("..."), '.', S("ab"));
    error += str_eq(r, S("ababab")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Large string (exceeds small string optimization)
    r = str_replace_char_str(alloc, S("a.b.c.d.e.f.g.h.i.j.k.l"), '.', S("::"));
    error += str_eq(r, S("a::b::c::d::e::f::g::h::i::j::k::l")) ? 0 : 1;
    str_deinit(alloc, &r);

    // Multi-char replacement that grows significantly
    r = str_replace_char_str(alloc, S("x.x.x"), '.', S("-----"));
    error += str_eq(r, S("x-----x-----x")) ? 0 : 1;
    str_deinit(alloc, &r);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_starts_with(void) {
    int error = 0;

    error += 1 == str_starts_with(S("abcdef"), S("abc")) ? 0 : 1;
    error += 1 == str_starts_with(S("abcdef"), S("abcdef")) ? 0 : 1;
    error += 1 == str_starts_with(S("abcdef"), S("")) ? 0 : 1;
    error += 0 == str_starts_with(S("abcdef"), S("x")) ? 0 : 1;
    error += 0 == str_starts_with(S("abcdef"), S("abcdefg")) ? 0 : 1;
    error += 1 == str_starts_with(S(""), S("")) ? 0 : 1;
    error += 0 == str_starts_with(S(""), S("a")) ? 0 : 1;

    return error;
}

static int test_str_empty_and_move(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    str        e     = str_empty();
    error += 1 == str_is_empty(e) ? 0 : 1;
    error += 0 == str_len(e) ? 0 : 1;

    str s = str_init(alloc, "hello");
    error += 0 == str_is_empty(s) ? 0 : 1;

    str moved = str_move(&s);
    error += 1 == str_is_empty(s) ? 0 : 1;
    error += str_eq(moved, S("hello")) ? 0 : 1;

    str_deinit(alloc, &moved);
    leak_detector_destroy(&alloc);
    return error;
}

static int test_resize(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // small -> big
    str s = str_init(alloc, "hi");
    error += 2 == str_len(s) ? 0 : 1;
    str_resize(alloc, &s, 20);
    error += 20 == str_len(s) ? 0 : 1;
    span sp = str_span(&s);
    error += sp.buf[0] == 'h' ? 0 : 1;
    error += sp.buf[1] == 'i' ? 0 : 1;

    // big -> big
    str_resize(alloc, &s, 30);
    error += 30 == str_len(s) ? 0 : 1;
    sp = str_span(&s);
    error += sp.buf[0] == 'h' ? 0 : 1;
    error += sp.buf[1] == 'i' ? 0 : 1;

    // big -> small
    str_resize(alloc, &s, 2);
    error += 2 == str_len(s) ? 0 : 1;
    sp = str_span(&s);
    error += sp.buf[0] == 'h' ? 0 : 1;
    error += sp.buf[1] == 'i' ? 0 : 1;

    str_deinit(alloc, &s);
    leak_detector_destroy(&alloc);
    return error;
}

static int test_slice(void) {
    int error = 0;

    str s     = S("abcdef");

    // normal slice
    span sl = str_slice_len(&s, 1, 3);
    error += 3 == sl.len ? 0 : 1;
    error += 0 == memcmp(sl.buf, "bcd", 3) ? 0 : 1;

    // slice past end clamps
    sl = str_slice_len(&s, 4, 100);
    error += 2 == sl.len ? 0 : 1;
    error += 0 == memcmp(sl.buf, "ef", 2) ? 0 : 1;

    // start past end
    sl = str_slice_len(&s, 100, 1);
    error += 0 == sl.len ? 0 : 1;

    // zero length
    sl = str_slice_len(&s, 0, 0);
    error += 0 == sl.len ? 0 : 1;

    // slice_left
    sl = str_slice_left(&s, 3);
    error += 3 == sl.len ? 0 : 1;
    error += 0 == memcmp(sl.buf, "def", 3) ? 0 : 1;

    // slice_left past end
    sl = str_slice_left(&s, 100);
    error += 0 == sl.len ? 0 : 1;

    return error;
}

static int test_hash(void) {
    int error = 0;

    // same input -> same output
    error += str_hash32(S("hello")) == str_hash32(S("hello")) ? 0 : 1;
    error += str_hash64(S("hello")) == str_hash64(S("hello")) ? 0 : 1;

    // different input -> likely different
    error += str_hash32(S("hello")) != str_hash32(S("world")) ? 0 : 1;
    error += str_hash64(S("hello")) != str_hash64(S("world")) ? 0 : 1;

    // empty string hashes consistently
    error += str_hash32(S("")) == str_hash32(S("")) ? 0 : 1;

    return error;
}

static int test_cstr(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // small string
    str         s  = str_init(alloc, "hi");
    char const *cs = str_cstr(&s);
    error += 0 == strcmp(cs, "hi") ? 0 : 1;
    str_deinit(alloc, &s);

    // big string
    s  = str_init(alloc, "this is a longer string for testing");
    cs = str_cstr(&s);
    error += 0 == strcmp(cs, "this is a longer string for testing") ? 0 : 1;
    str_deinit(alloc, &s);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_parse_num(void) {
    int error = 0;
    i64 i;
    u64 u;
    f64 f;

    // i64
    error += 1 == str_parse_num(S("42"), &i, &u, &f) ? 0 : 1;
    error += 42 == i ? 0 : 1;

    error += 1 == str_parse_num(S("-100"), &i, &u, &f) ? 0 : 1;
    error += -100 == i ? 0 : 1;

    // u64 (large positive)
    error += 2 == str_parse_num(S("18446744073709551615"), &i, &u, &f) ? 0 : 1;
    error += 18446744073709551615ULL == u ? 0 : 1;

    // f64
    error += 3 == str_parse_num(S("3.14"), &i, &u, &f) ? 0 : 1;
    error += f > 3.13 && f < 3.15 ? 0 : 1;

    // error
    error += 0 == str_parse_num(S("notanumber"), &i, &u, &f) ? 0 : 1;

    // oversized input rejected
    str big = S("1234567890123456789012345678901234567890123456789012345678901234");
    error += 0 == str_parse_num(big, &i, &u, &f) ? 0 : 1;

    return error;
}

static int test_init_num(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();
    i64        iv;
    u64        uv;
    f64        fv;

    // i64 round-trip
    str s = str_init_i64(alloc, -42);
    error += 1 == str_parse_num(s, &iv, &uv, &fv) ? 0 : 1;
    error += -42 == iv ? 0 : 1;
    str_deinit(alloc, &s);

    // u64 round-trip
    s = str_init_u64(alloc, 12345);
    error += 1 == str_parse_num(s, &iv, &uv, &fv) ? 0 : 1;
    error += 12345 == iv ? 0 : 1;
    str_deinit(alloc, &s);

    // f64 round-trip
    s = str_init_f64(alloc, 2.5);
    error += 3 == str_parse_num(s, &iv, &uv, &fv) ? 0 : 1;
    error += fv > 2.4 && fv < 2.6 ? 0 : 1;
    str_deinit(alloc, &s);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_parse_words(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // basic words
    str_array words = {.alloc = alloc};
    str_parse_words(S("hello world"), &words);
    error += 2 == words.size ? 0 : 1;
    if (words.size == 2) {
        error += str_eq(words.v[0], S("hello")) ? 0 : 1;
        error += str_eq(words.v[1], S("world")) ? 0 : 1;
    }
    for (u32 i = 0; i < words.size; ++i) str_deinit(alloc, &words.v[i]);
    array_free(words);

    // quoted string
    words = (str_array){.alloc = alloc};
    str_parse_words(S("foo \"bar baz\" qux"), &words);
    error += 3 == words.size ? 0 : 1;
    if (words.size == 3) {
        error += str_eq(words.v[0], S("foo")) ? 0 : 1;
        error += str_eq(words.v[1], S("\"bar baz\"")) ? 0 : 1;
        error += str_eq(words.v[2], S("qux")) ? 0 : 1;
    }
    for (u32 i = 0; i < words.size; ++i) str_deinit(alloc, &words.v[i]);
    array_free(words);

    // empty input
    words = (str_array){.alloc = alloc};
    str_parse_words(S(""), &words);
    error += 0 == words.size ? 0 : 1;
    array_free(words);

    // only spaces
    words = (str_array){.alloc = alloc};
    str_parse_words(S("   "), &words);
    error += 0 == words.size ? 0 : 1;
    array_free(words);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_cat_array(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // normal case
    str parts[] = {S("hello"), S(" "), S("world")};
    str r       = str_cat_array(alloc, (str_sized){.v = parts, .size = 3});
    error += str_eq(r, S("hello world")) ? 0 : 1;
    str_deinit(alloc, &r);

    // empty array
    r = str_cat_array(alloc, (str_sized){.v = null, .size = 0});
    error += str_is_empty(r) ? 0 : 1;

    // single element
    str one[] = {S("solo")};
    r         = str_cat_array(alloc, (str_sized){.v = one, .size = 1});
    error += str_eq(r, S("solo")) ? 0 : 1;
    str_deinit(alloc, &r);

    // all empty elements
    str empties[] = {S(""), S(""), S("")};
    r             = str_cat_array(alloc, (str_sized){.v = empties, .size = 3});
    error += str_is_empty(r) ? 0 : 1;

    leak_detector_destroy(&alloc);
    return error;
}

static int test_copy(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    // small string copy
    str s = str_init(alloc, "hi");
    str c = str_copy(alloc, s);
    error += str_eq(s, c) ? 0 : 1;
    str_deinit(alloc, &s);
    str_deinit(alloc, &c);

    // big string copy
    s = str_init(alloc, "this is a longer string for testing copy");
    c = str_copy(alloc, s);
    error += str_eq(s, c) ? 0 : 1;
    str_deinit(alloc, &s);
    str_deinit(alloc, &c);

    // copy_span
    str  orig = S("abcdef");
    span sp   = str_span(&orig);
    c         = str_copy_span(alloc, sp);
    error += str_eq(c, S("abcdef")) ? 0 : 1;
    str_deinit(alloc, &c);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_str_fmt(void) {
    int        error = 0;
    allocator *alloc = leak_detector_create();

    str        s     = str_fmt(alloc, "%s %d", "hello", 42);
    error += str_eq(s, S("hello 42")) ? 0 : 1;
    str_deinit(alloc, &s);

    s = str_fmt(alloc, "%d", 0);
    error += str_eq(s, S("0")) ? 0 : 1;
    str_deinit(alloc, &s);

    // longer format
    s = str_fmt(alloc, "the quick %s fox jumps over the %s dog", "brown", "lazy");
    error += str_eq(s, S("the quick brown fox jumps over the lazy dog")) ? 0 : 1;
    str_deinit(alloc, &s);

    leak_detector_destroy(&alloc);
    return error;
}

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int          error      = 0;
    int          this_error = 0;

    unsigned int seed       = (unsigned int)time(0);

    fprintf(stderr, "Seed = %u\n\n", seed);

    srand(seed);

    T(test_alloc);
    T(test_cat);
    T(test_dcat);
    T(test_build);
    T(test_ends_with);
    T(test_contains);
    T(test_contains_char);
    T(test_struct_layout);
    T(test_cmp);
    T(test_cat_multi);
    T(test_parse_cnum_binary);
    T(test_parse_cnum_z_suffix);
    T(test_prefix_char);
    T(test_rprefix_char);
    T(test_replace_char);
    T(test_replace_char_str);
    T(test_starts_with);
    T(test_str_empty_and_move);
    T(test_resize);
    T(test_slice);
    T(test_hash);
    T(test_cstr);
    T(test_parse_num);
    T(test_init_num);
    T(test_parse_words);
    T(test_cat_array);
    T(test_copy);
    T(test_str_fmt);

    return error;
}
