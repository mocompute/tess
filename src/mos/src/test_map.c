#include "map.h"
#include "map_internal.h"

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

static int test_structs(void) {
    int error = 0;

    dbg("map_cell_status size = %zu\n", sizeof(map_cell_status));
    dbg("map_header size = %zu\n", sizeof(map_header));
    error += 8 == sizeof(map_header) ? 0 : 1;
    return error;
}

static int test_map(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    map_t     *map   = map_create(alloc, sizeof(int), 8, 0);

    error += NULL == map_get(map, 0) ? 0 : 1;

    int data = 0;
    data     = 123;
    error += 0 == map_set(alloc, &map, 0, &data) ? 0 : 1;

    if (map_get(map, 0)) error += 123 == *(int *)map_get(map, 0) ? 0 : 1;
    else error++;

    error += 0 == map_get(map, 1) ? 0 : 1;
    data = 456;
    error += 0 == map_set(alloc, &map, 1, &data) ? 0 : 1;
    error += 456 == *(int *)map_get(map, 1) ? 0 : 1;

    map_erase(map, 0);
    error += 0 == map_get(map, 0) ? 0 : 1;
    error += 456 == *(int *)map_get(map, 1) ? 0 : 1;

    map_destroy(alloc, &map);

    return error;
}

static int test_big_map(void) {

    int          error = 0;

    size_t const N     = 100000;

    typedef struct pair_t {
        map_key left;
        int     right;

    } pair_t;

    allocator *alloc = alloc_default_allocator();
    vec_t      vec;
    if (vec_init(alloc, &vec, sizeof(pair_t), N)) {
        error++;
        goto cleanup;
    }

    map_t *map = map_create(alloc, sizeof(int), 8, 0);

    for (size_t i = 0; i < N; ++i) {
        // find unique key
        int key = rand();
        while (map_get(map, (map_key)key)) key = rand();

        pair_t pair = {(map_key)key, rand()};
        if (vec_push_back(alloc, &vec, &pair)) {
            goto cleanup;
        }
        if (map_set(alloc, &map, (map_key)pair.left, &pair.right)) {
            error++;
            goto cleanup;
        }
    }

    // verify
    for (size_t i = 0; i < vec_size(&vec); ++i) {
        pair_t *pair = vec_at(&vec, i);
        void   *res  = map_get(map, (map_key)pair->left);
        if (!res) {
            fprintf(stderr, "verify not found %zu: %u -> %i %p\n", i, pair->left, pair->right, res);
            error++;
            goto cleanup;
        }

        error += pair->right == *(int *)res ? 0 : 1;

        if (error) {
            fprintf(stderr, "verify failed %zu: %u -> %i (%p)\n", i, pair->left, pair->right, res);
            fprintf(stderr, "got %i instead\n", *(int *)res);
            error++;
            goto cleanup;
        }
    }

    map_destroy(alloc, &map);

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

    seed                    = 1755916792;
    fprintf(stderr, "Seed = %u\n\n", seed);

    srand(seed);

    T(test_structs);
    T(test_power_of_two);
    T(test_align);
    T(test_map);
    T(test_big_map);

    return error;
}
