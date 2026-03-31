#module main
#import <Alloc.tl>
#import <Unsafe.tl>

test_transient_realloc() {
    error := 0
    p1    := Alloc.context.transient.alloc(64zu)
    p2    := Alloc.context.transient.realloc(p1, 128zu)
    if p1 != p2 { error += 1 }
    error
}

test_transient_free_alloc() {
    error := 0
    p1    := Alloc.context.transient.alloc(64zu)
    Alloc.context.transient.dealloc(p1)
    p2 := Alloc.context.transient.alloc(128zu)
    if p1 != p2 { error += 1 }

    p3 := Alloc.context.transient.alloc(64zu)
    if p2 == p3 { error += 1 }
    error
}

test_transient_free_alloc_2() {
    error := 0
    p1    := Alloc.context.transient.alloc(64zu)
    p2    := Alloc.context.transient.alloc(128zu)
    if p2 <= p1 { error += 1 }

    Alloc.context.transient.dealloc(p1)

    p3 := Alloc.context.transient.alloc(64zu)
    if p3 <= p2 { error += 1 }
    error
}

// The following tests use bump allocators directly.

test_bump_create_destroy() {
    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)
    Alloc.BumpAllocator.destroy(bump)
    0
}

test_bump_realloc() {
    error := 0

    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)
    p1   := bump.alloc( 64zu)
    p2   := bump.realloc( p1, 128zu)
    if p1 != p2 { error += 1 }
    Alloc.BumpAllocator.destroy(bump)

    error
}


test_bump_realloc_non_contiguous() {
    error := 0

    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1 := bump.alloc( 64zu)
    p2 := bump.alloc( 16zu)
    p3 := bump.realloc( p1, 128zu)

    if p1 == p2 { error += 1 }
    if p1 == p3 { error += 1 }

    Alloc.BumpAllocator.destroy(bump)

    error
}

test_bump_free_reuse() {
    error := 0

    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1 := bump.alloc( 64zu)
    bump.dealloc( p1)
    p2 := bump.alloc( 128zu)

    if p1 != p2 { error += 1 }

    Alloc.BumpAllocator.destroy(bump)

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
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p: Ptr[CSize] := bump.clear( 4zu, sizeof[CSize]())
    // Check that calloc returns zeroed memory
    if p.* != 0zu { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_aligned_alloc() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 4096zu)

    // Allocate with 256-byte alignment, verify with independent modulo check
    p := bump.aligned_alloc( 64zu, 256zu)
    if p == null { error += 1 }
    if !Alloc.is_aligned(p, 256zu) { error += 1 }

    // Allocate with 128-byte alignment
    p2 := bump.aligned_alloc( 32zu, 128zu)
    if p2 == null { error += 1 }
    if !Alloc.is_aligned(p2, 128zu) { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_resize_last_block() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p := bump.alloc( 64zu)

    // Resize in place (last block, should succeed)
    p2 := bump.resize( p, 128zu)
    if p2 != p { error += 1 }

    // Shrink in place
    p3 := bump.resize( p, 32zu)
    if p3 != p { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_resize_non_last_block() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1 := bump.alloc( 64zu)
    p2 := bump.alloc( 64zu)

    // Resize p1 (not last block) - should fail
    p3 := bump.resize( p1, 128zu)
    if p3 != null { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_managed_resize_null() {
    error := 0
    p     := Alloc.context.managed.alloc(64zu)

    // Managed resize always returns null
    p2 := Alloc.context.managed.resize(p, 128zu)
    if p2 != null { error += 1 }

    Alloc.context.managed.dealloc(p)
    error
}

test_bump_reset() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1 := bump.alloc( 64zu)
    Alloc.BumpAllocator.reset(bump)
    p2 := bump.alloc( 64zu)

    // After reset, next allocation should reuse same address
    if p1 != p2 { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_stats() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    bump.alloc( 64zu)
    stats := Alloc.BumpAllocator.get_stats(bump)

    if stats.allocated == 0zu { error += 1 }
    if stats.capacity == 0zu { error += 1 }
    if stats.bucket_count != 1zu { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_stats_peak() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    bump.alloc( 256zu)
    Alloc.BumpAllocator.reset(bump)
    bump.alloc( 64zu)

    stats := Alloc.BumpAllocator.get_stats(bump)

    // Peak should be from before reset (256 + overhead), greater than current (64 + overhead)
    if stats.peak_allocated <= stats.allocated { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_save_restore() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1    := bump.alloc( 64zu)
    state := Alloc.BumpAllocator.save(bump)
    p2    := bump.alloc( 64zu)
    Alloc.BumpAllocator.restore(bump, state)
    p3 := bump.alloc( 64zu)

    // After restore, p3 should reuse p2's address
    if p2 != p3 { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

test_bump_save_restore_bucket_freed() {
    error := 0

    // Create a small bump allocator to force bucket creation
    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 128zu)

    bump.alloc( 32zu)
    state := Alloc.BumpAllocator.save(bump)

    // Allocate enough to trigger new bucket(s)
    bump.alloc( 256zu)
    stats_before := Alloc.BumpAllocator.get_stats(bump)
    if stats_before.bucket_count <= 1zu { error += 1 }

    Alloc.BumpAllocator.restore(bump, state)
    stats_after := Alloc.BumpAllocator.get_stats(bump)

    // Bucket count should decrease after restore
    if stats_after.bucket_count >= stats_before.bucket_count { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
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
