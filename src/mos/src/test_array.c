#include "array.h"

#include "alloc.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    array_header;
    int *v;
} int_array;

static int test_array(void) {
    int       error = 0;

    int_array arr   = {.alloc = leak_detector_create()};

    int       data  = 1;

    array_push(arr, data);
    data = 2;
    array_push(arr, data);
    error += arr.v[0] == 1 ? 0 : 1;
    error += arr.v[1] == 2 ? 0 : 1;

    array_free(arr);
    leak_detector_destroy(&arr.alloc);

    return error;
}

static int test_array_erase(void) {
    int error = 0;

    {
        int_array arr = {.alloc = leak_detector_create()};

        int       x   = 0;
        array_push(arr, x);
        x++;
        array_push(arr, x);
        x++;
        array_push(arr, x);

        array_erase(arr, 0);

        error += arr.size == 2 ? 0 : 1;
        error += arr.v[0] == 1 ? 0 : 1;
        error += arr.v[1] == 2 ? 0 : 1;

        array_free(arr);
        leak_detector_destroy(&arr.alloc);
    }

    {
        int_array arr = {.alloc = leak_detector_create()};

        int       x   = 0;
        array_push(arr, x);
        x++;
        array_push(arr, x);
        x++;
        array_push(arr, x);

        array_erase(arr, 1);

        error += arr.size == 2 ? 0 : 1;
        error += arr.v[0] == 0 ? 0 : 1;
        error += arr.v[1] == 2 ? 0 : 1;

        array_free(arr);
        leak_detector_destroy(&arr.alloc);
    }

    {
        int_array arr = {.alloc = leak_detector_create()};

        int       x   = 0;
        array_push(arr, x);
        x++;
        array_push(arr, x);
        x++;
        array_push(arr, x);

        array_erase(arr, 2);

        error += arr.size == 2 ? 0 : 1;
        error += arr.v[0] == 0 ? 0 : 1;
        error += arr.v[1] == 1 ? 0 : 1;

        array_free(arr);
        leak_detector_destroy(&arr.alloc);
    }

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
    T(test_array_erase);

    return error;
}
