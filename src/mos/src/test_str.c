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
    T(test_contains_char);
    T(test_struct_layout);
    T(test_cmp);
    T(test_cat_multi);
    T(test_parse_cnum_binary);
    T(test_prefix_char);
    T(test_replace_char);

    return error;
}
