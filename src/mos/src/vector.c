#include "vector.h"

#include "alloc.h"
#include "dbg.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

vec_t *vec_alloc(allocator *alloc) {
    return alloc->malloc(alloc, sizeof(vec_t));
}

void vec_dealloc(allocator *alloc, vec_t **p) {
    alloc->free(alloc, *p);
    *p = 0;
}

void vec_init_empty(vec_t *vec, size_t element_size) {
    alloc_zero(vec);
    vec->element_size = element_size;
}

int vec_init(allocator *alloc, vec_t *vec, size_t element_size, size_t initial_capacity) {
    assert(element_size <= PTRDIFF_MAX);
    alloc_zero(vec);
    vec->element_size = element_size;

    if (initial_capacity) {
        vec->data = alloc->malloc(alloc, sizeof(vec_data_header) + initial_capacity * element_size);
        if (NULL == vec->data) {
            dbg("vec_init: oom\n");
            return 1;
        }
        alloc_zero(vec->data);
        vec->data->capacity = initial_capacity;
    }
    return 0;
}

void vec_deinit(allocator *alloc, vec_t *vec) {
    alloc->free(alloc, vec->data);
    alloc_invalidate(vec);
}

int vec_reserve(allocator *alloc, vec_t *vec, size_t count) {

    if (vec_empty(vec)) return vec_init(alloc, vec, vec->element_size, count);

    if (vec->data->capacity >= count) return 0;

    size_t new_capacity = vec->data->capacity * 2;
    if (new_capacity == 0) new_capacity = 8;
    while (new_capacity < count) new_capacity *= 2;

    dbg("vec_reserve: realloc count %zu\n", count);
    assert(count < 10000000);
    void *p = alloc->realloc(alloc, vec->data, sizeof(vec_data_header) + new_capacity * vec->element_size);
    if (!p) {
        dbg("vec_reserve: oom\n");
        return 1;
    }

    vec->data           = p;
    vec->data->capacity = new_capacity;
    return 0;
}

void vec_move(vec_t *dst, vec_t *src) {
    alloc_copy(dst, src);
    alloc_invalidate(src);
}

bool vec_empty(vec_t const *vec) {
    return vec->data == NULL || vec->data->size == 0;
}

int vec_push_back(allocator *alloc, vec_t *vec, void const *element) {

    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + 1 : 1)) return 1;

    memcpy(vec->data->data + vec->data->size * vec->element_size, element, vec->element_size);
    vec->data->size += 1;
    return 0;
}

int vec_copy_back(allocator *alloc, vec_t *vec, void const *start, size_t count) {
    if (vec_reserve(alloc, vec, vec->data ? vec->data->size + count : count)) return 1;

    memcpy(vec->data->data + vec->data->size * vec->element_size, start, count * vec->element_size);
    vec->data->size += count;
    return 0;
}

void *vec_at(vec_t *vec, size_t index) {
    return vec->data->data + index * vec->element_size;
}

void *vec_back(vec_t *vec) {
    return vec->data->data + (vec->data->size - 1) * vec->element_size;
}

void vec_pop_back(vec_t *vec) {
    vec->data->size -= 1;
}

void vec_erase(vec_t *vec, char *it) {
    char const *const end = vec->data->data + vec->data->size * vec->element_size;
    ptrdiff_t         len = end - it - (ptrdiff_t)vec->element_size;

    memmove(it, it + vec->element_size, (size_t)len);
    vec->data->size -= 1;
}

nodiscard int vec_resize(allocator *alloc, vec_t *vec, size_t n) {
    if (vec_empty(vec)) return vec_init(alloc, vec, vec->element_size, n);

    if (n > vec->data->capacity)
        if (vec_reserve(alloc, vec, n)) {
            dbg("vec_resize: oom\n");
            return 1;
        }

    vec->data->size = n;
    return 0;
}

void vec_clear(vec_t *vec) {
    // Note: Do not free data.
    if (!vec_empty(vec)) vec->data->size = 0;
}

char *vec_data(vec_t *vec) {
    if (vec_empty(vec)) return NULL;
    return vec->data->data;
}

void *vec_begin(vec_t *vec) {
    if (vec_empty(vec)) return NULL;
    return vec->data->data;
}

void const *vec_cbegin(vec_t const *vec) {
    if (vec_empty(vec)) return NULL;
    return vec->data->data;
}

void const *vec_end(vec_t *vec) {
    // points 1 past the end
    if (vec_empty(vec)) return NULL;
    return vec->data->data + vec->data->size * vec->element_size;
}

size_t vec_size(vec_t const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->size;
}

size_t vec_capacity(vec_t const *vec) {
    if (vec_empty(vec)) return 0;
    return vec->data->capacity;
}

int vec_assoc_set(allocator *alloc, vec_t *vec, void const *pair) {
    assert(vec->element_size >= sizeof(size_t));
    if (vec_push_back(alloc, vec, pair)) {
        dbg("vec_assoc_set: oom\n");
        return 1;
    }
    return 0;
}

char *vec_assoc_get(vec_t *vec, size_t key) {
    if (vec_empty(vec)) return NULL;

    // From the back, search for an element whose first size_t field
    // matches the search term.

    char const *const last         = vec->data->data;
    char             *it           = vec_back(vec);
    size_t const      element_size = vec->element_size;

    while (1) {
        if (key == *(size_t *)it) return (it + sizeof(size_t)); // return second element of pair

        if (it == last) break; // examined last pair
        it -= element_size;
    }
    return NULL;
}

void vec_assoc_erase(vec_t *vec, size_t key) {

    char *it = vec_assoc_get(vec, key);
    if (!it) return;

    // it points just past the key, so reverse it
    it -= sizeof(size_t);

    vec_erase(vec, it);
}
