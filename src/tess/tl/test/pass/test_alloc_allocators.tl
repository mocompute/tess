#module main
#import <Alloc.tl>
#import <Unsafe.tl>

test_transient_realloc() {
    error := 0
    p1    := Alloc.transient_alloc(64zu)
    p2    := Alloc.transient_realloc(p1, 128zu)
    if p1 != p2 { error += 1 }
    error
}

test_transient_free_alloc() {
    error := 0
    p1    := Alloc.transient_alloc(64zu)
    Alloc.transient_free(p1)
    p2 := Alloc.transient_alloc(128zu)
    if p1 != p2 { error += 1 }

    p3 := Alloc.transient_alloc(64zu)
    if p2 == p3 { error += 1 }
    error
}

test_transient_free_alloc_2() {
    error := 0
    p1    := Alloc.transient_alloc(64zu)
    p2    := Alloc.transient_alloc(128zu)
    if p2 <= p1 { error += 1 }

    Alloc.transient_free(p1)

    p3 := Alloc.transient_alloc(64zu)
    if p3 <= p2 { error += 1 }
    error
}

// the following tests directly access low-level Alloc functions. Prefer to use the provided helper
// functions like Alloc.transient_alloc, etc.

test_bump_create_destroy() {
    bump := Alloc.bump_create(Alloc.context.managed, 1024zu)
    Alloc.bump_destroy(bump)
    0
}

test_bump_realloc() {
    error := 0

    bump := Alloc.bump_create(Alloc.context.managed, 1024zu)
    p1   := bump->malloc(bump, 64zu)
    p2   := bump->realloc(bump, p1, 128zu)
    if p1 != p2 { error += 1 }
    Alloc.bump_destroy(bump)

    error
}


test_bump_realloc_non_contiguous() {
    error := 0

    bump := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p1 := bump->malloc(bump, 64zu)
    p2 := bump->malloc(bump, 16zu)
    p3 := bump->realloc(bump, p1, 128zu)

    if p1 == p2 { error += 1 }
    if p1 == p3 { error += 1 }

    Alloc.bump_destroy(bump)

    error
}

test_bump_free_reuse() {
    error := 0

    bump := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p1 := bump->malloc(bump, 64zu)
    bump->free(bump, p1)
    p2 := bump->malloc(bump, 128zu)

    if p1 != p2 { error += 1 }

    Alloc.bump_destroy(bump)

    error
}

test_align_next_power_of_two() {
    error := 0

    error += if 1zu == Alloc.next_power_of_two(0zu) {0} else {1}
    error += if 1zu == Alloc.next_power_of_two(1zu << 0zu) {0} else {1}
    error += if 2zu == Alloc.next_power_of_two(1zu << 1zu) {0} else {1}
    error += if 4zu == Alloc.next_power_of_two(3zu) {0} else {1}
    error += if 4zu == Alloc.next_power_of_two(4zu) {0} else {1}
    error += if 8zu == Alloc.next_power_of_two(5zu) {0} else {1}

    error
}

test_bump_calloc_zero_fill() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p: Ptr[CSize] := bump->calloc(bump, 4zu, sizeof[CSize]())
    // Check that calloc returns zeroed memory
    if p.* != 0zu { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_aligned_alloc() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 4096zu)

    // Allocate with 256-byte alignment, verify with independent modulo check
    p := bump->aligned_alloc(bump, 64zu, 256zu)
    if p == null { error += 1 }
    if !Alloc.is_aligned(p, 256zu) { error += 1 }

    // Allocate with 128-byte alignment
    p2 := bump->aligned_alloc(bump, 32zu, 128zu)
    if p2 == null { error += 1 }
    if !Alloc.is_aligned(p2, 128zu) { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_resize_last_block() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p := bump->malloc(bump, 64zu)

    // Resize in place (last block, should succeed)
    p2 := bump->resize(bump, p, 128zu)
    if p2 != p { error += 1 }

    // Shrink in place
    p3 := bump->resize(bump, p, 32zu)
    if p3 != p { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_resize_non_last_block() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p1 := bump->malloc(bump, 64zu)
    p2 := bump->malloc(bump, 64zu)

    // Resize p1 (not last block) - should fail
    p3 := bump->resize(bump, p1, 128zu)
    if p3 != null { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_managed_resize_null() {
    error := 0
    p     := Alloc.managed_alloc(64zu)

    // Managed resize always returns null
    p2 := Alloc.managed_resize(p, 128zu)
    if p2 != null { error += 1 }

    Alloc.managed_free(p)
    error
}

test_bump_reset() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p1 := bump->malloc(bump, 64zu)
    Alloc.bump_reset(bump)
    p2 := bump->malloc(bump, 64zu)

    // After reset, next allocation should reuse same address
    if p1 != p2 { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_stats() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    bump->malloc(bump, 64zu)
    stats := Alloc.bump_get_stats(bump)

    if stats.allocated == 0zu { error += 1 }
    if stats.capacity == 0zu { error += 1 }
    if stats.bucket_count != 1zu { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_stats_peak() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    bump->malloc(bump, 256zu)
    Alloc.bump_reset(bump)
    bump->malloc(bump, 64zu)

    stats := Alloc.bump_get_stats(bump)

    // Peak should be from before reset (256 + overhead), greater than current (64 + overhead)
    if stats.peak_allocated <= stats.allocated { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_save_restore() {
    error := 0
    bump  := Alloc.bump_create(Alloc.context.managed, 1024zu)

    p1    := bump->malloc(bump, 64zu)
    state := Alloc.bump_save(bump)
    p2    := bump->malloc(bump, 64zu)
    Alloc.bump_restore(bump, state)
    p3 := bump->malloc(bump, 64zu)

    // After restore, p3 should reuse p2's address
    if p2 != p3 { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

test_bump_save_restore_bucket_freed() {
    error := 0

    // Create a small bump allocator to force bucket creation
    bump := Alloc.bump_create(Alloc.context.managed, 128zu)

    bump->malloc(bump, 32zu)
    state := Alloc.bump_save(bump)

    // Allocate enough to trigger new bucket(s)
    bump->malloc(bump, 256zu)
    stats_before := Alloc.bump_get_stats(bump)
    if stats_before.bucket_count <= 1zu { error += 1 }

    Alloc.bump_restore(bump, state)
    stats_after := Alloc.bump_get_stats(bump)

    // Bucket count should decrease after restore
    if stats_after.bucket_count >= stats_before.bucket_count { error += 1 }

    Alloc.bump_destroy(bump)
    error
}

main() {

    error := 0

    error += test_bump_create_destroy()
    error += test_bump_realloc()
    error += test_bump_realloc_non_contiguous()
    error += test_bump_free_reuse()
    error += test_align_next_power_of_two()

    error += test_transient_realloc()
    error += test_transient_free_alloc()
    error += test_transient_free_alloc_2()

    error += test_bump_calloc_zero_fill()
    error += test_bump_aligned_alloc()
    error += test_bump_resize_last_block()
    error += test_bump_resize_non_last_block()
    error += test_managed_resize_null()
    error += test_bump_reset()
    error += test_bump_stats()
    error += test_bump_stats_peak()
    error += test_bump_save_restore()
    error += test_bump_save_restore_bucket_freed()

    error

}
