#include "array.h"

#include "alloc.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    array_header;
    int *v;
} int_array;

int test_array(void) {
    int       error = 0;

    int_array arr   = {.alloc = alloc_leak_detector_create()};

    int       data  = 1;

    array_push(arr, &data);
    data = 2, array_push(arr, &data);
    error += arr.v[0] == 1 ? 0 : 1;
    error += arr.v[1] == 2 ? 0 : 1;

    array_free(arr);
    alloc_leak_detector_destroy(&arr.alloc);

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

    T(test_array);

    return error;
}
