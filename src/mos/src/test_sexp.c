#include "sexp.h"

#include "alloc.h"
#include "dbg.h"
#include "sexp_parser.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int test_sexp_assert(void) {
    assert(sizeof(void *) == 8);
    assert(sizeof(size_t) == 8);
    assert(sizeof(sexp) == 8);

    assert(offsetof(sexp_box, symbol.name) == offsetof(sexp_box, string.name));

    return 0;
}

static int test_sexp_parse(void) {
    int         error = 0;

    char const *input = "(a (b \"str\"  c)  d -123)";

    allocator  *alloc = alloc_default_allocator();

    sexp_parser p;
    if (sexp_parser_init(alloc, &p, input, strlen(input))) return error + 1;

    sexp         expr;
    sexp_err_tag err;
    size_t       err_pos;
    error += 0 == sexp_parser_next(&p, &expr, &err, &err_pos) ? 0 : 1;

    if (error) {
        dbg("error: %s\n", sexp_err_tag_to_string(err));
    } else {
        char *s = sexp_to_string(alloc, expr);
        if (null == s) return error + 1;
        dbg("parsed: %s\n", s);
        error += 0 == strcmp(s, "(a (b \"str\" c) d -123)") ? 0 : 1;
        alloc->free(alloc, s);

        sexp_deinit(alloc, &expr);
    }

    sexp_parser_deinit(&p);

    return error;
}

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    printf("INT64_MAX             = %" PRIi64 "\n", INT64_MAX);
    printf("INT64_MIN             = %" PRIi64 "\n", INT64_MIN);
    printf("INT64_MAX / 2         = %" PRIi64 "\n", INT64_MAX / 2);
    printf("INT64_MIN / 2         = %" PRIi64 "\n", INT64_MIN / 2);
    printf("INT64_MAX >> 1        = %" PRIi64 "\n", INT64_MAX >> 1);
    printf("INT64_MIN / 2         = %" PRIi64 "\n", INT64_MIN / 2);
    printf("INT64_MAX/2 << 1 >> 1 = %" PRIi64 "\n", ((INT64_MAX >> 1) * 2) >> 1);
    printf("INT64_MIN/2 << 1 >> 1 = %" PRIi64 "\n", ((INT64_MIN / 2) * 2) >> 1);

    T(test_sexp_assert);
    T(test_sexp_parse);

    return error;
}
