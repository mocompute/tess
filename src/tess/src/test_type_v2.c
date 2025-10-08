#include "alloc.h"
#include "dbg.h"
#include "str.h"
#include "v2_type.h"

#include <stdio.h>

static int test_type_variable_to_string(void) {
    int              error = 0;

    tl_type_variable tv    = 123;

    error += 0 == str_cmp_c(tl_type_variable_to_string(default_allocator(), &tv), "t123") ? 0 : 1;

    return error;
}

static int test_arrow_to_string(void) {
    int          error = 0;

    allocator   *alloc = arena_create(default_allocator(), 1024);
    tl_monotype *arrow =
      tl_monotype_create_arrow(alloc, tl_monotype_create(alloc, tl_monotype_init_tv(123)),
                               tl_monotype_create(alloc, tl_monotype_init_tv(456)));

    str res = tl_type_arrow_to_string(alloc, &arrow->arrow);
    error += 0 == str_cmp_c(res, "t123 -> t456") ? 0 : 1;

    str_deinit(alloc, &res);
    arena_destroy(default_allocator(), &alloc);
    return error;
}

// static int test_type_constructor_inst_to_string(void) {
//     int                      error = 0;

//     tl_type_constructor_inst cons  = 123;

//     error += 0 == str_cmp_c(tl_type_variable_to_string(default_allocator(), &tv), "t123") ? 0 : 1;

//     return error;
// }

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_type_variable_to_string);
    T(test_arrow_to_string);

    return error;
}
