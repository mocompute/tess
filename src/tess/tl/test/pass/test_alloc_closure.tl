#module main
#import <Alloc.tl>

// Combined alloc_closure tests

// --- basic: create, call, verify capture by value ---

test_basic() -> CInt {
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

test_no_captures() -> CInt {
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

test_multi_capture() -> CInt {
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

test_return() -> CInt {
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

test_chain() -> CInt {
    error := 0

    base := _ch_make_base(10)               // base(x) = x + 10
    l1   := _ch_add_layer(base, 5)          // offset=15, l1(x) = x + 15
    l2   := _ch_add_layer(l1, 3)            // offset=18, l2(x) = x + 18
    l3   := _ch_add_layer(l2, 2)            // offset=20, l3(x) = x + 20

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

test_deep_nest() -> CInt {
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

test_generic() -> CInt {
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

test_generic_capture() -> CInt {
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

test_struct() -> CInt {
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

test_alias_return() -> CInt {
    add10 := _ar_make_adder(10)
    error := 0
    error += if add10(5) == 15 { 0 } else { 1 }
    error += if add10(0) == 10 { 0 } else { 1 }
    error
}

// --- block_scope: allocated closure created inside a block expression ---

test_block_scope() -> CInt {
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

test_capture_closure() -> CInt {
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

test_capture_fn_call() -> CInt {
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

test_explicit_allocator() -> CInt {
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

test_generic_struct() -> CInt {
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

test_if_return() -> CInt {
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

test_in_struct_field() -> CInt {
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

test_mixed_stack() -> CInt {
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

_mpf_test() -> CInt {
    n           := 10
    f           := [[alloc, capture(n)]] (x) { x + n }
    m           := 20
    g           := [[alloc, capture(m)]] (x) { x + m }
    error: CInt := 0
    error += if f(1) == 11 { 0 } else { 1 }
    error += if g(1) == 21 { 0 } else { 1 }
    error
}

test_multi_per_function() -> CInt { _mpf_test() }

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

test_nested_alloc_expr() -> CInt {
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

test_no_leak() -> CInt {
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

test_shorthand_return() -> CInt {
    add5        := _sr_make_adder(5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error
}

// --- weak_int_default: weak integer defaulting at specialization boundary ---

test_weak_int_default() -> CInt {
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

// --- main: run all tests ---

main() {
    error := test_basic()
    error += test_no_captures()
    error += test_multi_capture()
    error += test_return()
    error += test_chain()
    error += test_deep_nest()
    error += test_generic()
    error += test_generic_capture()
    error += test_struct()
    error += test_alias_return()
    error += test_block_scope()
    error += test_capture_closure()
    error += test_capture_fn_call()
    error += test_explicit_allocator()
    error += test_generic_struct()
    error += test_if_return()
    error += test_in_struct_field()
    error += test_mixed_stack()
    error += test_multi_per_function()
    error += test_nested_alloc_expr()
    error += test_no_leak()
    error += test_shorthand_return()
    error += test_weak_int_default()
    error
}
