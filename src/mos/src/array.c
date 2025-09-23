#include "array.h"
#include "alloc.h"

#include <assert.h>
#include <string.h>

void *array_alloc_impl(array_header_t *h, u32 num, u32 width, u16 align) {
    assert(h->alloc);
    width = (u32)alloc_align(width, align);
    return alloc_malloc(h->alloc, num * width);
}

void array_free_impl(array_header_t *h, void *ptr) {
    assert(h->alloc);
    alloc_free(h->alloc, ptr);
}

void *array_realloc(array_header_t *h, void *ptr, u32 num, u32 width, u16 align) {
    assert(h->alloc);
    width = (u32)alloc_align(width, align);
    return alloc_realloc(h->alloc, ptr, num * width);
}

void *array_reserve_impl(array_header_t *h, void *ptr, u32 num, u32 width, u16 align) {
    assert(h->alloc);
    width = (u32)alloc_align(width, align);
    if (num > h->capacity) {
        void *new_ptr = ptr;
        if (ptr) new_ptr = array_realloc(h, ptr, num, width, align);
        else new_ptr = array_alloc_impl(h, num, width, align);
        h->capacity = num;
        return new_ptr;
    }
    return ptr;
}

void *array_push_impl(array_header_t *h, void *restrict ptr, u32 width, u16 align,
                      void const *restrict data) {
    assert(h->alloc);
    if (h->size == h->capacity) {
        u32 new_cap = h->capacity ? h->capacity * 2 : 8;
        ptr         = array_reserve_impl(h, ptr, new_cap, width, align);
        h->capacity = new_cap;
    }
    memcpy(&ptr[h->size++ * alloc_align(width, align)], data, width);
    return ptr;
}

int array_contains_impl(array_header_t *h, void *restrict ptr, u32 width, u16 align,
                        void const *restrict data) {

    if (0 == h->size) return 0;
    u32 actual_align = alloc_align(width, align);

    for (u32 i = 0; i < h->size; ++i) {
        if (0 == memcmp(&ptr[i * actual_align], data, width)) return 1;
    }
    return 0;
}

void *array_copy_impl(array_header_t *h, void *restrict ptr, u32 width, u16 align,
                      void const *restrict data, u32 num) {
    ptr = array_reserve_impl(h, ptr, h->size + num, width, align);

    memcpy(&ptr[h->size * alloc_align(width, align)], data, num * width);
    h->size += num;
    return ptr;
}

void *array_move_impl(array_header_t *h, void *ptr, u32 width, u16 align, void *data, u32 num) {
    ptr = array_reserve_impl(h, ptr, h->size + num, width, align);

    memmove(&ptr[h->size * alloc_align(width, align)], data, num * width);
    h->size += num;
    return ptr;
}

void *array_insert_impl(array_header_t *h, void *restrict ptr, u32 index, u32 width, u16 align,
                        void const *restrict data, u32 num) {
    assert(index < h->size);

    ptr            = array_reserve_impl(h, ptr, h->size + num, width, align);

    size_t aligned = alloc_align(width, align);

    memmove(&ptr[(index + num) * aligned], &ptr[index * aligned], (h->size - index) * aligned);
    memcpy(&ptr[index * aligned], data, num * width);

    h->size += num;
    return ptr;
}

void array_erase_impl(array_header_t *h, void *ptr, u32 index, u32 width, u16 align) {
    assert(index < h->size);

    size_t aligned = alloc_align(width, align);

    memmove(&ptr[index * aligned], &ptr[(index + 1) * aligned], (h->size - index - 1) * aligned);

    h->size--;
}

void *array_shrink_impl(array_header_t *h, void *ptr, u32 width, u16 align) {
    assert(h->alloc);
    if (h->capacity == h->size) return ptr;

    ptr         = array_realloc(h, ptr, h->size, width, align);
    h->capacity = h->size;
    return ptr;
}

char_cslice char_cslice_from(char const *str, u32 len) {
    return (char_cslice){.v = str, .end = len};
}

void *array_insert_sorted_impl(array_header_t *h, void *restrict ptr, u32 width, u16 align,
                               void const *restrict data, array_cmp_fun cmp) {

    size_t aligned = alloc_align(width, align);

    assert(h->alloc);
    if (h->size == h->capacity) {
        u32 new_cap = h->capacity ? h->capacity * 2 : 8;
        ptr         = array_reserve_impl(h, ptr, new_cap, width, align);
        h->capacity = new_cap;
    }

    u32 index = 0;
    for (; index < h->size; ++index)
        if (cmp(data, &ptr[index * aligned]) >= 0) break;

    if (index < h->size)
        memmove(&ptr[(index + 1) * aligned], &ptr[index * aligned], (h->size - index) * aligned);

    memcpy(&ptr[index * aligned], data, width);
    h->size++;

    return ptr;
}
