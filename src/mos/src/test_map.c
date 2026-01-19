#include "alloc.h"
#include "hashmap.h"

#include "array.h"
#include "dbg.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int test_power_of_two(void) {
    int error = 0;

    error += 1 == alloc_next_power_of_two(0) ? 0 : 1;
    error += 1 == alloc_next_power_of_two(1) ? 0 : 1;
    error += 2 == alloc_next_power_of_two(2) ? 0 : 1;
    error += 4 == alloc_next_power_of_two(3) ? 0 : 1;
    error += 4 == alloc_next_power_of_two(4) ? 0 : 1;
    error += 8 == alloc_next_power_of_two(5) ? 0 : 1;
    error += 8 == alloc_next_power_of_two(8) ? 0 : 1;
    error += 16 == alloc_next_power_of_two(9) ? 0 : 1;
    error += 16 == alloc_next_power_of_two(16) ? 0 : 1;
    error += 32 == alloc_next_power_of_two(17) ? 0 : 1;

    return error;
}

static int test_align(void) {
    int error = 0;

    assert(8 == sizeof(void *));

    error += 8 == alloc_align_to_pointer_size(2) ? 0 : 1;
    error += 8 == alloc_align_to_pointer_size(4) ? 0 : 1;
    error += 8 == alloc_align_to_pointer_size(6) ? 0 : 1;
    error += 8 == alloc_align_to_pointer_size(8) ? 0 : 1;
    error += 16 == alloc_align_to_pointer_size(9) ? 0 : 1;
    return error;
}

static int test_map(void) {
    int        error = 0;

    allocator *alloc = leak_detector_create();

    hashmap   *map   = map_create(alloc, sizeof(int), 16);

    int        key0  = 0;
    error += null == map_get(map, &key0, sizeof key0) ? 0 : 1;

    int data = 0;
    data     = 123;
    map_set(&map, &key0, sizeof key0, &data);

    if (map_get(map, &key0, sizeof key0)) error += 123 == *(int *)map_get(map, &key0, sizeof key0) ? 0 : 1;
    else error++;

    hashmap_iterator iter = {0};
    map_iter(map, &iter);

    int iterkey;
    memcpy(&iterkey, iter.key_ptr, iter.key_size); // iter.key_ptr is not aligned
    error += iterkey == 0 ? 0 : 1;
    error += sizeof(int) == iter.key_size ? 0 : 1;

    error += *(int *)iter.data == 123 ? 0 : 1;
    error += 0 == map_iter(map, &iter) ? 0 : 1;

    int key1 = 1;
    error += 0 == map_get(map, &key1, sizeof key1) ? 0 : 1;
    data = 456;
    map_set(&map, &key1, sizeof key1, &data);
    error += 456 == *(int *)map_get(map, &key1, sizeof key1) ? 0 : 1;

    map_erase(map, &key0, sizeof key0);
    error += 0 == map_get(map, &key0, sizeof key0) ? 0 : 1;
    error += 456 == *(int *)map_get(map, &key1, sizeof key1) ? 0 : 1;

    map_destroy(&map);

    leak_detector_destroy(&alloc);
    return error;
}

static int test_big_map(void) {

    int          error = 0;

    #if MOS_WINDOWS
    // MSVC gives us rand() in range [0, 32767]
    size_t const N     = 10000;
    #else
    size_t const N     = 100000;
    #endif

    typedef struct pair_t {
        int left;
        int right;
    } pair_t;

    defarray(pair_array, pair_t);

    allocator *alloc = default_allocator();

    pair_array vec   = {.alloc = alloc};
    array_reserve(vec, N);

    hashmap *map = map_create(alloc, sizeof(int), 1024);

    for (size_t i = 0; i < N; ++i) {
        // find unique key
        int key = rand();
        int tries = 1000;
        while (tries-- && map_contains(map, &key, sizeof key)) {
            key = rand();
            fprintf(stderr, "next key == %i\n", key);
        }
        if (-1 == tries)
            fatal("loop exhausted");

        pair_t pair = {key, rand()};
        array_push(vec, pair);
        map_set(&map, &pair.left, sizeof pair.left, &pair.right);
    }

    // verify
    for (u32 i = 0; i < vec.size; ++i) {
        pair_t *pair = &vec.v[i];
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
    array_free(vec);
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
