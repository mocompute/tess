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

static int test_align_to_pointer_size(void) {
    int    error   = 0;

    size_t pointer = sizeof(void *);

    error += 0 == alloc_align_to_pointer_size(0) ? 0 : 1;
    error += pointer == alloc_align_to_pointer_size(1) ? 0 : 1;
    error += pointer == alloc_align_to_pointer_size(pointer) ? 0 : 1;
    error += 2 * pointer == alloc_align_to_pointer_size(pointer + 1) ? 0 : 1;
    error += 2 * pointer == alloc_align_to_pointer_size(pointer * 2) ? 0 : 1;
    error += 3 * pointer == alloc_align_to_pointer_size(pointer * 2 + 1) ? 0 : 1;

    return error;
}

static int test_align(void) {
    int error = 0;

    error += 0 == alloc_align(0, 1) ? 0 : 1;
    error += 1 == alloc_align(1, 1) ? 0 : 1;

    error += 0 == alloc_align(0, 2) ? 0 : 1;
    error += 2 == alloc_align(1, 2) ? 0 : 1;
    error += 2 == alloc_align(2, 2) ? 0 : 1;
    error += 4 == alloc_align(3, 2) ? 0 : 1;

    return error;
}

static int test_arena_realloc(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    void      *p2    = alloc_realloc(arena, p1, 128);

    error += p1 == p2 ? 0 : 1;

    arena_destroy(&arena);

    return error;
}

static int test_arena_realloc_non_contiguous(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    void      *p2    = alloc_malloc(arena, 16);
    void      *p3    = alloc_realloc(arena, p1, 128);

    error += p1 != p2 ? 0 : 1;
    error += p1 != p3 ? 0 : 1;

    arena_destroy(&arena);

    return error;
}

static int test_arena_free_reuse(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    alloc_free(arena, p1);

    void *p2 = alloc_malloc(arena, 128);

    error += p1 == p2 ? 0 : 1;

    arena_destroy(&arena);

    return error;
}

static int test_arena_free_reuse_non_contiguous(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    void      *p2    = alloc_malloc(arena, 64);
    alloc_free(arena, p1);

    void *p3 = alloc_malloc(arena, 128);

    error += p1 != p3 ? 0 : 1;
    error += p2 != p3 ? 0 : 1;

    arena_destroy(&arena);

    return error;
}

static int test_arena_save_restore_basic(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    (void)p1;

    arena_watermark wm = arena_save(arena);
    void           *p2 = alloc_malloc(arena, 128);
    (void)p2;

    arena_restore(arena, wm);

    // After restore, next alloc should reuse the space
    void *p3 = alloc_malloc(arena, 128);
    error += p2 == p3 ? 0 : 1;

    arena_destroy(&arena);
    return error;
}

static int test_arena_save_restore_nested(void) {
    int        error = 0;

    allocator *arena = arena_create(default_allocator(), 1024);

    void      *p1    = alloc_malloc(arena, 64);
    (void)p1;

    arena_watermark wm_outer = arena_save(arena);
    void           *p2       = alloc_malloc(arena, 32);
    (void)p2;

    arena_watermark wm_inner = arena_save(arena);
    void           *p3       = alloc_malloc(arena, 32);
    (void)p3;

    arena_restore(arena, wm_inner);

    // Inner restore: p3's space is reclaimed, p2 survives
    void *p4 = alloc_malloc(arena, 32);
    error += p3 == p4 ? 0 : 1; // should reuse p3's slot

    arena_restore(arena, wm_outer);

    // Outer restore: both p2 and p4's space are reclaimed
    void *p5 = alloc_malloc(arena, 32);
    error += p2 == p5 ? 0 : 1; // should reuse p2's slot

    arena_destroy(&arena);
    return error;
}

static int test_arena_save_restore_multi_bucket(void) {
    int error = 0;

    // Small initial size to force multiple buckets
    allocator *arena = arena_create(default_allocator(), 64);

    void      *p1    = alloc_malloc(arena, 32);
    (void)p1;

    arena_watermark wm = arena_save(arena);

    // Allocate enough to spill into new buckets
    for (int i = 0; i < 100; i++) {
        void *p = alloc_malloc(arena, 64);
        (void)p;
    }

    arena_stats stats_before;
    arena_get_stats(arena, &stats_before);

    arena_restore(arena, wm);

    arena_stats stats_after;
    arena_get_stats(arena, &stats_after);

    // After restore, allocated bytes should be much less
    error += stats_after.allocated < stats_before.allocated ? 0 : 1;
    // Capacity should be retained (buckets not freed)
    error += stats_after.capacity == stats_before.capacity ? 0 : 1;

    arena_destroy(&arena);
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
    T(test_align_to_pointer_size);
    T(test_align);
    T(test_arena_realloc);
    T(test_arena_realloc_non_contiguous);
    T(test_arena_free_reuse);
    T(test_arena_free_reuse_non_contiguous);
    T(test_arena_save_restore_basic);
    T(test_arena_save_restore_nested);
    T(test_arena_save_restore_multi_bucket);

    return error;
}
