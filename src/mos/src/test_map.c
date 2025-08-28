#include "alloc.h"
#include "hashmap.h"

#include "dbg.h"
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

    allocator *alloc = alloc_leak_detector_create();

    hashmap   *map   = map_create(alloc, sizeof(int));

    int        key0  = 0;
    error += null == map_get(map, &key0, sizeof key0) ? 0 : 1;

    int data = 0;
    data     = 123;
    map_set(&map, &key0, sizeof key0, &data);

    if (map_get(map, &key0, sizeof key0)) error += 123 == *(int *)map_get(map, &key0, sizeof key0) ? 0 : 1;
    else error++;

    int key1 = 1;
    error += 0 == map_get(map, &key1, sizeof key1) ? 0 : 1;
    data = 456;
    map_set(&map, &key1, sizeof key1, &data);
    error += 456 == *(int *)map_get(map, &key1, sizeof key1) ? 0 : 1;

    map_erase(map, &key0, sizeof key0);
    error += 0 == map_get(map, &key0, sizeof key0) ? 0 : 1;
    error += 456 == *(int *)map_get(map, &key1, sizeof key1) ? 0 : 1;

    map_destroy(&map);

    alloc_leak_detector_destroy(&alloc);
    return error;
}

static int test_big_map(void) {

    int          error = 0;

    size_t const N     = 100000;

    typedef struct pair_t {
        int left;
        int right;

    } pair_t;

    allocator *alloc = alloc_default_allocator();
    vector     vec   = VEC(pair_t);
    vec_reserve(alloc, &vec, N);

    hashmap *map = map_create(alloc, sizeof(int));

    for (size_t i = 0; i < N; ++i) {
        // find unique key
        int key = rand();
        while (map_contains(map, &key, sizeof key)) key = rand();

        pair_t pair = {key, rand()};
        vec_push_back(alloc, &vec, &pair);
        map_set(&map, &pair.left, sizeof pair.left, &pair.right);
    }

    // verify
    for (u32 i = 0; i < vec_size(&vec); ++i) {
        pair_t *pair = vec_at(&vec, i);
        void   *res  = map_get(map, &pair->left, sizeof pair->left);
        if (!res) {
            fprintf(stderr, "verify not found %u: %u -> %i %p\n", i, pair->left, pair->right, res);
            error++;
            goto cleanup;
        }

        error += pair->right == *(int *)res ? 0 : 1;

        if (error) {
            fprintf(stderr, "verify failed %u: %u -> %i (%p)\n", i, pair->left, pair->right, res);
            fprintf(stderr, "got %i instead\n", *(int *)res);
            error++;
            goto cleanup;
        }
    }

    map_destroy(&map);

cleanup:
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

    fprintf(stderr, "Seed = %u\n\n", seed);

    srand(seed);

    T(test_power_of_two);
    T(test_align);
    T(test_map);
    T(test_big_map);

    return error;
}
