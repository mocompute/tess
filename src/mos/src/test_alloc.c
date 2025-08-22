#include "alloc.h"

#include <stdio.h>

static int test_align_next_power_of_two(void) {
    int error = 0;

    error += 1 == alloc_next_power_of_two(0) ? 0 : 1;
    error += 1 == alloc_next_power_of_two(1 << 0) ? 0 : 1;
    error += 2 == alloc_next_power_of_two(1 << 1) ? 0 : 1;
    error += 4 == alloc_next_power_of_two(3) ? 0 : 1;
    error += 4 == alloc_next_power_of_two(4) ? 0 : 1;
    error += 8 == alloc_next_power_of_two(5) ? 0 : 1;

    return error;
}

static int test_align_to_word_size(void) {
    int    error = 0;

    size_t word  = sizeof(void *);

    error += 0 == alloc_align_to_word_size(0) ? 0 : 1;
    error += word == alloc_align_to_word_size(1) ? 0 : 1;
    error += word == alloc_align_to_word_size(word) ? 0 : 1;
    error += 2 * word == alloc_align_to_word_size(word + 1) ? 0 : 1;
    error += 2 * word == alloc_align_to_word_size(word * 2) ? 0 : 1;
    error += 3 * word == alloc_align_to_word_size(word * 2 + 1) ? 0 : 1;

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

    T(test_align_next_power_of_two);
    T(test_align_to_word_size);

    return error;
}
