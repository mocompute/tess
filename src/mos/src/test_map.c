#include "map.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int test_power_of_two(void) {
    int error = 0;

    error += 1 == map_next_power_of_two(0) ? 0 : 1;
    error += 1 == map_next_power_of_two(1) ? 0 : 1;
    error += 2 == map_next_power_of_two(2) ? 0 : 1;
    error += 4 == map_next_power_of_two(3) ? 0 : 1;
    error += 4 == map_next_power_of_two(4) ? 0 : 1;
    error += 8 == map_next_power_of_two(5) ? 0 : 1;
    error += 8 == map_next_power_of_two(8) ? 0 : 1;
    error += 16 == map_next_power_of_two(9) ? 0 : 1;
    error += 16 == map_next_power_of_two(16) ? 0 : 1;
    error += 32 == map_next_power_of_two(17) ? 0 : 1;

    return error;
}

static int test_align(void) {
    int error = 0;

    assert(8 == sizeof(void *));

    error += 8 == alloc_align_to_word_size(2) ? 0 : 1;
    error += 8 == alloc_align_to_word_size(4) ? 0 : 1;
    error += 8 == alloc_align_to_word_size(6) ? 0 : 1;
    error += 8 == alloc_align_to_word_size(8) ? 0 : 1;
    error += 16 == alloc_align_to_word_size(9) ? 0 : 1;
    return error;
}

static int test_map(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    map_t     *map   = map_alloc(alloc);

    if (map_init(alloc, map, sizeof(int), 8, 0)) return 1;

    int data = 0;

    error += 0 == map_get(map, 0) ? 0 : 1;

    data = 123;
    error += 0 == map_set(alloc, map, 0, &data) ? 0 : 1;
    if (map_get(map, 0)) error += 123 == *(int *)map_get(map, 0) ? 0 : 1;
    else error++;
    // error += 123 == *(int *)map_get(map, 0) ? 0 : 1;

    error += 0 == map_get(map, 1) ? 0 : 1;
    data = 456;
    error += 0 == map_set(alloc, map, 1, &data) ? 0 : 1;
    error += 456 == *(int *)map_get(map, 1) ? 0 : 1;

    map_erase(map, 0);
    error += 0 == map_get(map, 0) ? 0 : 1;
    error += 456 == *(int *)map_get(map, 1) ? 0 : 1;

    map_deinit(alloc, map);
    map_dealloc(alloc, &map);

    return error;
}

static int test_big_map(void) {
    int          error = 0;

    size_t const N     = 1000000;

    typedef struct pair_t {
        ptrdiff_t left, right;
    } pair_t;

    allocator *alloc = alloc_default_allocator();
    vec_t      vec;
    if (vec_init(alloc, &vec, sizeof(pair_t), N)) return error + 1;

    map_t *map = map_alloc(alloc);
    if (map_init(alloc, map, sizeof(ptrdiff_t), 8, 0)) return error + 1;

    for (size_t i = 0; i < N; ++i) {
        // find unique key
        int key = rand();
        while (map_get(map, (map_key)key)) key = rand();

        pair_t pair = {key, rand()};
        if (vec_push_back(alloc, &vec, &pair)) {
            return 1;
        }
        if (map_set(alloc, map, (map_key)pair.left, &pair.right)) return 1;
    }

    // verify
    for (size_t i = 0; i < vec_size(&vec); ++i) {
        pair_t *pair = vec_at(&vec, i);
        void   *res  = map_get(map, (map_key)pair->left);
        if (!res) {
            fprintf(stderr, "verify not found %zu: %zu -> %zu %p\n", i, pair->left, pair->right, res);
            return (error + 1);
        }

        error += pair->right == *(int *)res ? 0 : 1;

        if (error) {
            fprintf(stderr, "verify failed %zu: %zu -> %zu (%p)\n", i, pair->left, pair->right, res);
            fprintf(stderr, "got %i instead\n", *(int *)res);
            return (error + 1);
        }
    }

    map_deinit(alloc, map);
    map_dealloc(alloc, &map);

    vec_deinit(alloc, &vec);
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

    // seed = 1755508043;
    fprintf(stderr, "Seed = %u\n\n", seed);

    srand(seed);

    T(test_power_of_two);
    T(test_align);
    T(test_map);
    T(test_big_map);

    return error;
}
