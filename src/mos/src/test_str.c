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
    T(test_struct_layout);

    return error;
}
