#include "util.h"

#include <stdio.h>

static int test_min_max(void) {
    int error = 0;

    error += 5 == max(1, 5) ? 0 : 1;
    error += 5 == max(5, 1) ? 0 : 1;

    error += 1 == min(1, 5) ? 0 : 1;
    error += 1 == min(5, 1) ? 0 : 1;

    int a = 1, b = 2;
    swap(a, b);
    error += a == 2 ? 0 : 1;
    error += b == 1 ? 0 : 1;

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

    T(test_min_max)

    return error;
}
