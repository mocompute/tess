#module main
#import <Alloc.tl>
#import <Unsafe.tl>

// ============================================================
// From test_alloc_align.tl
// ============================================================

test_align() {
    error := 0
    // Note: must use intrinsic because c_max_align_t is a C typedef
    max_align := _tl_sizeof_(c_max_align_t)

    error += if sizeof[Ptr[Void]]() <= max_align {0} else {1}
    error += if sizeof[Ptr[Void]]() >= sizeof[CSize]() {0} else {1}

    if sizeof[Ptr[Void]]() < max_align {
        error += if Alloc.align_max(sizeof[Ptr[Void]]() + 1zu) == max_align {0} else {1}
    }
    else {
        error += if Alloc.align_max(max_align + 1zu) == 2zu * max_align {0} else {1}
    }
    error
}

// ============================================================
// From test_alloc_allocators.tl
// ============================================================

_allocators_test_transient_realloc() {
    error := 0
    p1    := Alloc.context.transient.alloc(64zu)
    p2    := Alloc.context.transient.realloc(p1, 128zu)
    if p1 != p2 { error += 1 }
    error
}

_allocators_test_transient_free_alloc() {
    error := 0
    p1    := Alloc.context.transient.alloc(64zu)
    Alloc.context.transient.dealloc(p1)
    p2 := Alloc.context.transient.alloc(128zu)
    if p1 != p2 { error += 1 }

    p3 := Alloc.context.transient.alloc(64zu)
    if p2 == p3 { error += 1 }
    error
}

_allocators_test_transient_free_alloc_2() {
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

_allocators_test_bump_create_destroy() {
    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)
    Alloc.BumpAllocator.destroy(bump)
    0
}

_allocators_test_bump_realloc() {
    error := 0

    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)
    p1   := bump.alloc( 64zu)
    p2   := bump.realloc( p1, 128zu)
    if p1 != p2 { error += 1 }
    Alloc.BumpAllocator.destroy(bump)

    error
}


_allocators_test_bump_realloc_non_contiguous() {
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

_allocators_test_bump_free_reuse() {
    error := 0

    bump := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p1 := bump.alloc( 64zu)
    bump.dealloc( p1)
    p2 := bump.alloc( 128zu)

    if p1 != p2 { error += 1 }

    Alloc.BumpAllocator.destroy(bump)

    error
}

_allocators_test_align_next_power_of_two() {
    error := 0

    error += if 1zu == Alloc.next_power_of_two(0zu) {0} else {1}
    error += if 1zu == Alloc.next_power_of_two(1zu << 0zu) {0} else {1}
    error += if 2zu == Alloc.next_power_of_two(1zu << 1zu) {0} else {1}
    error += if 4zu == Alloc.next_power_of_two(3zu) {0} else {1}
    error += if 4zu == Alloc.next_power_of_two(4zu) {0} else {1}
    error += if 8zu == Alloc.next_power_of_two(5zu) {0} else {1}

    error
}

_allocators_test_bump_calloc_zero_fill() {
    error := 0
    bump  := Alloc.BumpAllocator.create(Alloc.context.managed, 1024zu)

    p: Ptr[CSize] := bump.clear( 4zu, sizeof[CSize]())
    // Check that calloc returns zeroed memory
    if p.* != 0zu { error += 1 }

    Alloc.BumpAllocator.destroy(bump)
    error
}

_allocators_test_bump_aligned_alloc() {
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

_allocators_test_bump_resize_last_block() {
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

_allocators_test_bump_resize_non_last_block() {
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

_allocators_test_managed_resize_null() {
    error := 0
    p     := Alloc.context.managed.alloc(64zu)

    // Managed resize always returns null
    p2 := Alloc.context.managed.resize(p, 128zu)
    if p2 != null { error += 1 }

    Alloc.context.managed.dealloc(p)
    error
}

_allocators_test_bump_reset() {
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

_allocators_test_bump_stats() {
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

_allocators_test_bump_stats_peak() {
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

_allocators_test_bump_save_restore() {
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

_allocators_test_bump_save_restore_bucket_freed() {
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

test_allocators() {

    error := 0

    error += _allocators_test_bump_create_destroy()
    error += _allocators_test_bump_realloc()
    error += _allocators_test_bump_realloc_non_contiguous()
    error += _allocators_test_bump_free_reuse()
    error += _allocators_test_align_next_power_of_two()

    error += _allocators_test_transient_realloc()
    error += _allocators_test_transient_free_alloc()
    error += _allocators_test_transient_free_alloc_2()

    error += _allocators_test_bump_calloc_zero_fill()
    error += _allocators_test_bump_aligned_alloc()
    error += _allocators_test_bump_resize_last_block()
    error += _allocators_test_bump_resize_non_last_block()
    error += _allocators_test_managed_resize_null()
    error += _allocators_test_bump_reset()
    error += _allocators_test_bump_stats()
    error += _allocators_test_bump_stats_peak()
    error += _allocators_test_bump_save_restore()
    error += _allocators_test_bump_save_restore_bucket_freed()

    error

}

// ============================================================
// From test_alloc_closure.tl
// ============================================================

// --- basic: create, call, verify capture by value ---

_closure_test_basic() {
    n: Int := 10
    f      := [[alloc, capture(n)]] (x) { x + n }

    error: CInt := 0
    error += if f(5) == 15 { 0 } else { 1 }
    error += if f(32) == 42 { 0 } else { 1 }

    // capture is by value: mutating n doesn't affect f
    n = 99
    error += if f(5) == 15 { 0 } else { 1 }
    error
}

// --- no_captures: [[alloc]] with zero captures ---

_nc_make_doubler() -> (Int) -> Int {
    f := [[alloc]] (x) { x + x }
    f
}

_closure_test_no_captures() {
    f     := _nc_make_doubler()
    error := 0
    error += if f(21) == 42 { 0 } else { 1 }
    error += if f(0) == 0 { 0 } else { 1 }
    error
}

// --- multi_capture: capture multiple variables of different types ---

_mc_compute(n: Int, flag: Bool) -> (Int) -> Int {
    f := [[alloc, capture(n, flag)]] (x) {
        if flag { x + n } else { x - n }
    }
    f
}

_closure_test_multi_capture() {
    add := _mc_compute(10, true)
    sub := _mc_compute(10, false)

    error := 0
    error += if add(5) == 15 { 0 } else { 1 }
    error += if sub(5) == -5 { 0 } else { 1 }

    // capture is by value: mutating originals doesn't affect closures
    error += if add(0) == 10 { 0 } else { 1 }
    error += if sub(0) == -10 { 0 } else { 1 }
    error
}

// --- return: returning an allocated closure from a function ---

_ret_outer(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_closure_test_return() {
    g     := _ret_outer(10)
    error := 0
    error += if g(5) == 15 { 0 } else { 1 }
    error
}

// --- chain: each closure captures result of calling previous one ---

_ch_make_base(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_ch_add_layer(base: (Int) -> Int, extra: Int) -> (Int) -> Int {
    offset := base(0) + extra
    f      := [[alloc, capture(offset)]] (x) { x + offset }
    f
}

_closure_test_chain() {
    error := 0

    base := _ch_make_base(10)           // base(x) = x + 10
    l1   := _ch_add_layer(base, 5)      // offset=15, l1(x) = x + 15
    l2   := _ch_add_layer(l1, 3)        // offset=18, l2(x) = x + 18
    l3   := _ch_add_layer(l2, 2)        // offset=20, l3(x) = x + 20

    error += if base(0) == 10 { 0 } else { 1 }
    error += if l1(0) == 15 { 0 } else { 1 }
    error += if l2(0) == 18 { 0 } else { 1 }
    error += if l3(0) == 20 { 0 } else { 1 }
    error += if l3(100) == 120 { 0 } else { 1 }

    error
}

// --- deep_nest: deeply nested function calls returning allocated closures ---

_dn_level0(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_dn_level1(n: Int) -> (Int) -> Int {
    f   := _dn_level0(n)
    val := f(n)
    // val = n + n = 2n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

_dn_level2(n: Int) -> (Int) -> Int {
    f   := _dn_level1(n)
    val := f(n)
    // val = n + 2n = 3n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

_dn_level3(n: Int) -> (Int) -> Int {
    f   := _dn_level2(n)
    val := f(n)
    // val = n + 3n = 4n
    g := [[alloc, capture(val)]] (x) { x + val }
    g
}

_closure_test_deep_nest() {
    error := 0
    f     := _dn_level3(10)
    // level3(10): val = 10 + 30 = 40, so f(0) = 40
    error += if f(0) == 40 { 0 } else { 1 }
    // f(5) = 5 + 40 = 45
    error += if f(5) == 45 { 0 } else { 1 }
    error
}

// --- generic: generic allocated closure with no captures ---

_gen_make_id[T]() -> (T) -> T {
    f := [[alloc]] (x) { x }
    f
}

_closure_test_generic() {
    f_int  := _gen_make_id[Int]()
    f_bool := _gen_make_id[Bool]()
    error  := 0
    error += if f_int(42) == 42 { 0 } else { 1 }
    error += if f_bool(true) == true { 0 } else { 1 }
    error
}

// --- generic_capture: generic closure with captures, specialized with different types ---

_gc_make_adder[T](n: T) -> (T) -> T {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_closure_test_generic_capture() {
    add5  := _gc_make_adder[Int](5)
    add1p := _gc_make_adder[Float](1.5)
    error := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error += if add1p(2.5) == 4.0 { 0 } else { 1 }
    error
}

// --- struct: storing allocated closure in struct field ---

Processor: { run: (Int) -> Int }

_st_make_processor(n: Int) -> Processor {
    f := [[alloc, capture(n)]] (x) { x + n }
    Processor(run = f)
}

_closure_test_struct() {
    p := _st_make_processor(10)
    r := p.run(5)
    if r == 15 { 0 } else { 1 }
}

// --- alias_return: returning through a symbol alias ---

_ar_make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    g := f
    g
}

_closure_test_alias_return() {
    add10 := _ar_make_adder(10)
    error := 0
    error += if add10(5) == 15 { 0 } else { 1 }
    error += if add10(0) == 10 { 0 } else { 1 }
    error
}

// --- block_scope: allocated closure created inside a block expression ---

_closure_test_block_scope() {
    error: CInt := 0
    ( {
     n := 100
     f := [[alloc, capture(n)]] (x) { x + n }
     error += if f(5) == 105 { 0 } else { 1 }
     void
     })
    error
}

// --- capture_closure: capture another closure by value ---

_cc_make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_cc_apply_twice(f: (Int) -> Int, x: Int) -> (Int) -> Int {
    result := f(f(x))
    g      := [[alloc, capture(result)]] (y) { result + y }
    g
}

_closure_test_capture_closure() {
    add5 := _cc_make_adder(5)
    // apply_twice(add5, 10) => add5(add5(10)) = add5(15) = 20
    // then captures result=20
    g := _cc_apply_twice(add5, 10)

    error := 0
    error += if g(0) == 20 { 0 } else { 1 }
    error += if g(2) == 22 { 0 } else { 1 }
    error
}

// --- capture_fn_call: capturing a function-typed value and calling it ---

_cf_make_base(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

_cf_add_layer(base: (Int) -> Int, extra: Int) -> (Int) -> Int {
    f := [[alloc, capture(base, extra)]] (x) { base(x) + extra }
    f
}

_closure_test_capture_fn_call() {
    base        := _cf_make_base(10)
    l1          := _cf_add_layer(base, 5)
    error: CInt := 0
    error += if l1(0) == 15 { 0 } else { 1 }
    error
}

// --- explicit_allocator: [[alloc(expr)]] with explicit Ptr[Allocator] ---

_ea_make_adder(n: Int, a: Ptr[Alloc.Allocator]) -> (Int) -> Int {
    f := [[alloc(a), capture(n)]] (x) { x + n }
    f
}

_closure_test_explicit_allocator() {
    // Use the default allocator explicitly
    a     := Alloc.context.default
    add10 := _ea_make_adder(10, a)

    error := 0
    error += if add10(5) == 15 { 0 } else { 1 }
    error += if add10(32) == 42 { 0 } else { 1 }
    error
}

// --- generic_struct: allocated closure in generic struct field ---

GenericProcessor[T]: { run: (T) -> T }

_gs_make_processor(n: Int) -> GenericProcessor[Int] {
    f := [[alloc, capture(n)]] (x) { x + n }
    GenericProcessor[Int](run = f)
}

_closure_test_generic_struct() {
    p := _gs_make_processor(10)
    r := p.run(5)
    if r == 15 { 0 } else { 1 }
}

// --- if_return: returning allocated closure from if/else expression ---

_ir_pick(use_add: Bool, n: Int) -> (Int) -> Int {
    if use_add {
        f := [[alloc, capture(n)]] (x) { x + n }
        f
    } else {
        f := [[alloc, capture(n)]] (x) { x * n }
        f
    }
}

_closure_test_if_return() {
    add5        := _ir_pick(true, 5)
    mul5        := _ir_pick(false, 5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error += if mul5(10) == 50 { 0 } else { 1 }
    error
}

// --- in_struct_field: structs with multiple closure fields ---

Transform: { forward: (Int) -> Int, backward: (Int) -> Int }

_sf_make_transform(offset: Int) -> Transform {
    fwd := [[alloc, capture(offset)]] (x) { x + offset }
    bwd := [[alloc, capture(offset)]] (x) { x - offset }
    Transform(forward = fwd, backward = bwd)
}

_sf_round_trip(t: Transform, x: Int) -> Int {
    t.backward(t.forward(x))
}

_closure_test_in_struct_field() {
    error := 0

    t := _sf_make_transform(42)
    error += if t.forward(10) == 52 { 0 } else { 1 }
    error += if t.backward(52) == 10 { 0 } else { 1 }

    // Round-trip should be identity
    error += if _sf_round_trip(t, 100) == 100 { 0 } else { 1 }
    error += if _sf_round_trip(t, 0) == 0 { 0 } else { 1 }

    // Multiple transforms are independent
    t2 := _sf_make_transform(7)
    error += if t.forward(0) == 42 { 0 } else { 1 }
    error += if t2.forward(0) == 7 { 0 } else { 1 }

    error
}

// --- mixed_stack: mixing stack and allocated closures ---

_ms_apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

_closure_test_mixed_stack() {
    error := 0

    // Stack closure captures by reference
    n       := 10
    stack_f := (x) { x + n }
    error += if stack_f(5) == 15 { 0 } else { 1 }

    // Allocated closure captures by value
    alloc_f := [[alloc, capture(n)]] (x) { x + n }
    error += if alloc_f(5) == 15 { 0 } else { 1 }

    // Mutate n — stack closure sees it, allocated doesn't
    n = 99
    error += if stack_f(1) == 100 { 0 } else { 1 } // by ref: sees 99
    error += if alloc_f(1) == 11 { 0 } else { 1 }  // by val: still 10

    // Both can be passed to the same HOF
    error += if _ms_apply(stack_f, 1) == 100 { 0 } else { 1 }
    error += if _ms_apply(alloc_f, 1) == 11 { 0 } else { 1 }

    error
}

// --- multi_per_function: multiple allocated closures in same function ---

_mpf_test() {
    n           := 10
    f           := [[alloc, capture(n)]] (x) { x + n }
    m           := 20
    g           := [[alloc, capture(m)]] (x) { x + m }
    error: CInt := 0
    error += if f(1) == 11 { 0 } else { 1 }
    error += if g(1) == 21 { 0 } else { 1 }
    error
}

_closure_test_multi_per_function() { _mpf_test() }

// --- nested_alloc_expr: nested allocated closures with inner alloc_expr ---

_nae_apply(f: (Int) -> Int, x: Int) -> Int {
    f(x)
}

_nae_outer(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) {
        // Inner closure uses [[alloc]] (default allocator).
        // The default allocator reference must NOT appear as a free variable
        // of the outer closure.
        inner := [[alloc, capture(x, n)]] (y) { x + y + n }
        _nae_apply(inner, x)
    }
    f
}

_closure_test_nested_alloc_expr() {
    add   := _nae_outer(10)
    error := 0
    error += if add(5) == 20 { 0 } else { 1 }
    error += if add(0) == 10 { 0 } else { 1 }
    error
}

// --- no_leak: verify closure calls don't allocate ---

_nl_make_adder(n: Int, a: Ptr[Alloc.Allocator]) -> (Int) -> Int {
    f := [[alloc(a), capture(n)]] (x) { x + n }
    f
}

_closure_test_no_leak() {
    arena := Alloc.BumpAllocator.create(Alloc.context.default, 4096zu)

    add10 := _nl_make_adder(10, arena)

    // Snapshot stats after closure creation
    stats_before := Alloc.BumpAllocator.get_stats(arena)

    // Call the closure many times
    sum := 0
    i   := 0
    while i < 100 {
        sum += add10(i)
        i += 1
    }

    // Snapshot stats after calls
    stats_after := Alloc.BumpAllocator.get_stats(arena)

    error := 0

    // Verify correct results
    // sum = sum(i + 10, i=0..99) = sum(i, i=0..99) + 1000 = 4950 + 1000 = 5950
    error += if sum == 5950 { 0 } else { 1 }

    // Verify no new allocations occurred during calls
    error += if stats_before.peak_allocated == stats_after.peak_allocated { 0 } else { 1 }

    error
}

// --- shorthand_return: direct lambda as implicit return value ---

_sr_make_adder(n: Int) -> (Int) -> Int {
    [[alloc, capture(n)]] (x) { x + n }
}

_closure_test_shorthand_return() {
    add5        := _sr_make_adder(5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error
}

// --- weak_int_default: weak integer defaulting at specialization boundary ---

_closure_test_weak_int_default() {
    error: CInt := 0

    // Weak int from literal argument
    n := 100
    f := [[alloc, capture(n)]] (x) { x + n }
    error += if f(5) == 105 { 0 } else { 1 }

    // Unsigned weak int (literal is used in unsigned context)
    size: CUnsignedLongLong := 10
    g                       := [[alloc, capture(size)]] (x: CUnsignedLongLong) { x + size }
    error += if g(5) == 15 { 0 } else { 1 }

    // Multiple weak int params
    a := 1
    b := 2
    h := [[alloc, capture(a, b)]] (x) { x + a + b }
    error += if h(10) == 13 { 0 } else { 1 }

    error
}

test_closure() {
    error := _closure_test_basic()
    error += _closure_test_no_captures()
    error += _closure_test_multi_capture()
    error += _closure_test_return()
    error += _closure_test_chain()
    error += _closure_test_deep_nest()
    error += _closure_test_generic()
    error += _closure_test_generic_capture()
    error += _closure_test_struct()
    error += _closure_test_alias_return()
    error += _closure_test_block_scope()
    error += _closure_test_capture_closure()
    error += _closure_test_capture_fn_call()
    error += _closure_test_explicit_allocator()
    error += _closure_test_generic_struct()
    error += _closure_test_if_return()
    error += _closure_test_in_struct_field()
    error += _closure_test_mixed_stack()
    error += _closure_test_multi_per_function()
    error += _closure_test_nested_alloc_expr()
    error += _closure_test_no_leak()
    error += _closure_test_shorthand_return()
    error += _closure_test_weak_int_default()
    error
}

// ============================================================
// From test_alloc_expr_type_valid.tl
// ============================================================

// [[alloc(arena)]] where arena is Ptr[Allocator] must pass type checking.

test_expr_type_valid() {
    bump        := Alloc.BumpAllocator.create(Alloc.context.transient, 1024zu)
    n           := 42
    f           := [[alloc(bump), capture(n)]] (x: Int) { x + n }
    error: CInt := 0
    if f(10) != 52 { error += 1 }
    error
}

// ============================================================
// From test_alloc_method_call.tl
// ============================================================

// Test: Allocator receiver methods (alloc, clear, realloc, dealloc, aligned_alloc, aligned_dealloc, resize).

test_method_call() {
    a     := Alloc.context.default
    error := 0

    // alloc + dealloc
    p := a.alloc(64)
    if p == null { error += 1 }
    a.dealloc(p)

    // clear (zero-initialized)
    p2: Ptr[CSize] := a.clear(4, sizeof[CSize]())
    if p2 == null { error += 1 }
    if p2.* != 0 { error += 1 }
    a.dealloc(p2)

    // realloc
    p3 := a.alloc(32)
    p4 := a.realloc(p3, 128)
    if p4 == null { error += 1 }
    a.dealloc(p4)

    // aligned_alloc + aligned_dealloc
    p5 := a.aligned_alloc(64, 256)
    if p5 == null { error += 1 }
    if !Alloc.is_aligned(p5, 256) { error += 1 }
    a.aligned_dealloc(p5)

    // resize (managed allocator returns null — no in-place resize)
    p6 := a.alloc(64)
    p7 := a.resize(p6, 128)
    if p7 != null { error += 1 }
    a.dealloc(p6)

    error
}

// ============================================================
// Main: run all test groups
// ============================================================

main() {
    error := test_align()
    error += test_allocators()
    error += test_closure()
    error += test_expr_type_valid()
    error += test_method_call()
    error
}
