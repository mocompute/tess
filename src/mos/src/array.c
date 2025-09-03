#include "array.h"
#include "alloc.h"
#include <string.h>

void *array_alloc_impl(array_header_t *h, u32 num, u32 width, u16 align) {
    width = (u32)alloc_align(width, align);
    return alloc_malloc(h->alloc, num * width);
}

void array_free_impl(array_header_t *h, void *ptr) {
    alloc_free(h->alloc, ptr);
}

void *array_realloc(array_header_t *h, void *ptr, u32 num, u32 width, u16 align) {
    width = (u32)alloc_align(width, align);
    return alloc_realloc(h->alloc, ptr, num * width);
}

void *array_reserve_impl(array_header_t *h, void *ptr, u32 num, u32 width, u16 align) {
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
    if (h->size == h->capacity) {
        u32 new_cap = h->capacity ? h->capacity * 2 : 8;
        ptr         = array_reserve_impl(h, ptr, new_cap, width, align);
        h->capacity = new_cap;
    }
    memcpy(&ptr[h->size++ * alloc_align(width, align)], data, width);
    return ptr;
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

void *array_shrink_impl(array_header_t *h, void *ptr, u32 width, u16 align) {
    if (h->capacity == h->size) return ptr;

    ptr         = array_realloc(h, ptr, h->size, width, align);
    h->capacity = h->size;
    return ptr;
}
