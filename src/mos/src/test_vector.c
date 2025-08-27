#include "vector.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int test_vector(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    vector    *vec   = vec_create(alloc, 2, sizeof(int));

    error += vec_empty(vec) == 1 ? 0 : 1;

    int val = 123;
    vec_push_back(alloc, vec, &val);
    error += vec_empty(vec) == 0 ? 0 : 1;
    error += *(int *)(vec_back(vec)) == 123 ? 0 : 1;

    val = 456;
    vec_push_back(alloc, vec, &val);
    error += vec_empty(vec) == 0 ? 0 : 1;
    error += *(int *)(vec_back(vec)) == 456 ? 0 : 1;

    vec_pop_back(vec);
    error += vec_empty(vec) == 0 ? 0 : 1;
    error += *(int *)(vec_back(vec)) == 123 ? 0 : 1;

    vec_pop_back(vec);
    error += vec_empty(vec) == 1 ? 0 : 1;

    int data[] = {321, 234, 654};
    vec_copy_back(alloc, vec, data, 3);
    error += 3 == vec_size(vec) ? 0 : 1;
    error += 3 <= vec_capacity(vec) ? 0 : 1;
    error += ((int *)vec_data(vec))[0] == 321 ? 0 : 1;
    error += ((int *)vec_data(vec))[1] == 234 ? 0 : 1;
    error += ((int *)vec_data(vec))[2] == 654 ? 0 : 1;

    vec_destroy(alloc, &vec);

    return error;
}

static int test_assoc(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    vector    *vec   = vec_create(alloc, 0, 2 * sizeof(u32));
    u32        pair[2];

    pair[0] = 1;
    pair[1] = 2;
    vec_assoc_set(alloc, vec, pair);
    error += 2 == *(u32 *)vec_assoc_get(vec, 1) ? 0 : 1;

    pair[0] = 2;
    pair[1] = 3;
    vec_assoc_set(alloc, vec, pair);
    error += 2 == *(u32 *)vec_assoc_get(vec, 1) ? 0 : 1;
    error += 3 == *(u32 *)vec_assoc_get(vec, 2) ? 0 : 1;

    pair[0] = 1;
    pair[1] = 99;
    vec_assoc_set(alloc, vec, pair);
    error += 99 == *(u32 *)vec_assoc_get(vec, 1) ? 0 : 1;
    error += 3 == *(u32 *)vec_assoc_get(vec, 2) ? 0 : 1;

    // note that this erase only removes the first match
    vec_assoc_erase(vec, 1);
    error += 2 == *(u32 *)vec_assoc_get(vec, 1) ? 0 : 1;
    error += 3 == *(u32 *)vec_assoc_get(vec, 2) ? 0 : 1;

    // the second erase will remove the original value we set
    vec_assoc_erase(vec, 1);
    error += 0 == vec_assoc_get(vec, 1) ? 0 : 1;
    error += 3 == *(u32 *)vec_assoc_get(vec, 2) ? 0 : 1;

    error += 0 == vec_assoc_get(vec, 999) ? 0 : 1;

    vec_destroy(alloc, &vec);

    return error;
}

static int test_assoc_set(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    // no payload, just the key
    vector *vec = vec_create(alloc, 0, sizeof(size_t));

    size_t  key = 1;
    vec_assoc_set(alloc, vec, &key);

    error += 0 != vec_assoc_get(vec, 1) ? 0 : 1;
    error += 0 == vec_assoc_get(vec, 999) ? 0 : 1;

    vec_destroy(alloc, &vec);
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

    assert(16 == sizeof(struct vector));

    T(test_vector);
    T(test_assoc);
    T(test_assoc_set);

    return error;
}
