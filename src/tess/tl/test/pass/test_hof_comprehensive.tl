#module main
#import <Array.tl>
#import <Alloc.tl>

// =============================================================================
// Combined HOF closure tests.
// Original files:
//   test_hof_closure_nested.tl
//   test_hof_closure_compose.tl
//   test_hof_closure_generic.tl
//   test_hof_closure_in_struct.tl
//   test_hof_closure_loop_capture.tl
//   test_hof_returned_closure.tl
//   test_hof_stack_closure_capture.tl
//   test_hof_alloc_closure.tl
// =============================================================================

// ---------------------------------------------------------------------------
// Helpers from test_hof_closure_nested.tl
// ---------------------------------------------------------------------------

nested_make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

// ---------------------------------------------------------------------------
// Tests from test_hof_closure_nested.tl
// ---------------------------------------------------------------------------

test_closure_capturing_closure() {
    error := 0
    add5  := nested_make_adder(5)
    // Stack closure captures the allocated closure add5
    arr := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, (x) { add5(x) })
    if result.data.[0] != 6 { error += 1 }
    if result.data.[1] != 7 { error += 1 }
    if result.data.[2] != 8 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_alloc_closure_capturing_closure() {
    error := 0
    add10 := nested_make_adder(10)
    // Allocated closure captures another allocated closure
    doubled := [[alloc, capture(add10)]] (x) { add10(add10(x)) }
    arr     := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, doubled)
    if result.data.[0] != 21 { error += 1 }
    if result.data.[1] != 22 { error += 1 }
    if result.data.[2] != 23 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_nested_with_filter() {
    error := 0
    add3  := nested_make_adder(3)
    // Filter using a closure that calls another closure
    arr := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    // Keep elements where add3(x) > 6, i.e. x > 3
    result := Array.filter(arr, (x) { add3(x) > 6 })
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_nested_reduce() {
    error := 0
    add1  := nested_make_adder(1)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    // reduce: acc + add1(x) = acc + x + 1
    sum := Array.reduce(arr, 0, (acc, x) { acc + add1(x) })
    // (10+1) + (20+1) + (30+1) = 63
    if sum != 63 { error += 1 }
    Array.free(arr.&)
    error
}

// ---------------------------------------------------------------------------
// Tests from test_hof_closure_compose.tl
// ---------------------------------------------------------------------------

test_filter_then_map() {
    error          := 0
    threshold: Int := 2
    offset:    Int := 100
    arr            := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    filtered := Array.filter(arr, (x) { x > threshold })
    result   := Array.map(filtered, (x) { x + offset })
    if result.size != 3zu { error += 1 }
    if result.data.[0] != 103 { error += 1 }
    if result.data.[1] != 104 { error += 1 }
    if result.data.[2] != 105 { error += 1 }
    Array.free(arr.&)
    Array.free(filtered.&)
    Array.free(result.&)
    error
}

test_map_then_reduce() {
    error      := 0
    scale: Int := 10
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    mapped := Array.map(arr, (x) { x * scale })
    sum    := Array.reduce(mapped, 0, (acc, x) { acc + x })
    if sum != 60 { error += 1 }
    Array.free(arr.&)
    Array.free(mapped.&)
    error
}

test_full_pipeline() {
    // filter > 2, map *10, reduce sum, all with captures
    error          := 0
    threshold: Int := 2
    scale:     Int := 10
    arr            := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    filtered := Array.filter(arr, (x) { x > threshold })
    mapped   := Array.map(filtered, (x) { x * scale })
    sum      := Array.reduce(mapped, 0, (acc, x) { acc + x })
    // (3+4+5)*10 = 120
    if sum != 120 { error += 1 }
    Array.free(arr.&)
    Array.free(filtered.&)
    Array.free(mapped.&)
    error
}

test_pipeline_alloc_closures() {
    // Same pipeline but with allocated closures
    error          := 0
    threshold: Int := 2
    scale:     Int := 10
    pred           := [[alloc, capture(threshold)]] (x) { x > threshold }
    mapper         := [[alloc, capture(scale)]] (x) { x * scale }
    arr            := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    filtered := Array.filter(arr, pred)
    mapped   := Array.map(filtered, mapper)
    sum      := Array.reduce(mapped, 0, (acc, x) { acc + x })
    if sum != 120 { error += 1 }
    Array.free(arr.&)
    Array.free(filtered.&)
    Array.free(mapped.&)
    error
}

test_sort_then_filter() {
    error          := 0
    threshold: Int := 3
    arr            := Array.empty[Int]()
    Array.push(arr.&, 5)
    Array.push(arr.&, 1)
    Array.push(arr.&, 4)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.sort(arr.&, (a, b) { a - b })
    result := Array.filter(arr, (x) { x > threshold })
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

// ---------------------------------------------------------------------------
// Helpers from test_hof_closure_generic.tl
// ---------------------------------------------------------------------------

apply_to_each[T](arr: Array[T], f: (T) -> T) -> Array[T] {
    result   := Array.empty[T]()
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        Array.push(result.&, f(arr.data.[i]))
    }
    result
}

count_matching[T](arr: Array[T], pred: (T) -> Bool) -> Int {
    n: Int   := 0
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if pred(arr.data.[i]) { n += 1 }
    }
    n
}

fold_right[T, U](arr: Array[T], init: U, f: (T, U) -> U) -> U {
    acc := init
    i   := arr.size
    while i > 0zu {
        i   = i - 1zu
        acc = f(arr.data.[i], acc)
    }
    acc
}

// ---------------------------------------------------------------------------
// Tests from test_hof_closure_generic.tl
// ---------------------------------------------------------------------------

test_generic_hof_stack_closure() {
    error       := 0
    offset: Int := 10
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := apply_to_each(arr, (x) { x + offset })
    if result.data.[0] != 11 { error += 1 }
    if result.data.[1] != 12 { error += 1 }
    if result.data.[2] != 13 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_generic_hof_alloc_closure() {
    error       := 0
    offset: Int := 10
    f           := [[alloc, capture(offset)]] (x) { x + offset }
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := apply_to_each(arr, f)
    if result.data.[0] != 11 { error += 1 }
    if result.data.[1] != 12 { error += 1 }
    if result.data.[2] != 13 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_count_matching_stack() {
    error      := 0
    bound: Int := 2
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    n := count_matching(arr, (x) { x > bound })
    if n != 2 { error += 1 }
    Array.free(arr.&)
    error
}

test_count_matching_alloc() {
    error      := 0
    bound: Int := 2
    pred       := [[alloc, capture(bound)]] (x) { x > bound }
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    n := count_matching(arr, pred)
    if n != 2 { error += 1 }
    Array.free(arr.&)
    error
}

test_fold_right() {
    error      := 0
    scale: Int := 10
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    // fold_right: f(1, f(2, f(3, 0))) = 1*10 + 2*10 + 3*10 = 60
    result := fold_right(arr, 0, (x, acc) { acc + x * scale })
    if result != 60 { error += 1 }
    Array.free(arr.&)
    error
}

// ---------------------------------------------------------------------------
// Helpers from test_hof_closure_in_struct.tl
// ---------------------------------------------------------------------------

Transformer: { transform: (Int) -> Int, predicate: (Int) -> Bool }

make_transformer(offset: Int, threshold: Int) -> Transformer {
    t := [[alloc, capture(offset)]] (x) { x + offset }
    p := [[alloc, capture(threshold)]] (x) { x > threshold }
    Transformer(transform = t, predicate = p)
}

// ---------------------------------------------------------------------------
// Tests from test_hof_closure_in_struct.tl
// ---------------------------------------------------------------------------

test_struct_map() {
    error := 0
    tx    := make_transformer(100, 0)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, tx.transform)
    if result.data.[0] != 101 { error += 1 }
    if result.data.[1] != 102 { error += 1 }
    if result.data.[2] != 103 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_struct_filter() {
    error := 0
    tx    := make_transformer(0, 3)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, tx.predicate)
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_struct_pipeline() {
    error := 0
    tx    := make_transformer(10, 15)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 6)
    Array.push(arr.&, 9)
    // map +10 -> [13, 16, 19], filter >15 -> [16, 19]
    mapped   := Array.map(arr, tx.transform)
    filtered := Array.filter(mapped, tx.predicate)
    if filtered.size != 2zu { error += 1 }
    if filtered.data.[0] != 16 { error += 1 }
    if filtered.data.[1] != 19 { error += 1 }
    Array.free(arr.&)
    Array.free(mapped.&)
    Array.free(filtered.&)
    error
}

test_multiple_structs() {
    error := 0
    tx1   := make_transformer(10, 0)
    tx2   := make_transformer(20, 0)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    r1 := Array.map(arr, tx1.transform)
    r2 := Array.map(arr, tx2.transform)
    if r1.data.[0] != 11 { error += 1 }
    if r1.data.[1] != 12 { error += 1 }
    if r2.data.[0] != 21 { error += 1 }
    if r2.data.[1] != 22 { error += 1 }
    Array.free(arr.&)
    Array.free(r1.&)
    Array.free(r2.&)
    error
}

// ---------------------------------------------------------------------------
// Tests from test_hof_closure_loop_capture.tl
// ---------------------------------------------------------------------------

test_closure_loop_capture() {
    error := 0

    closures := Array.empty[(Int) -> Int]()

    // Create closures in a loop, each capturing current value of i
    i := 0
    while i < 5, i += 1 {
        f := [[alloc, capture(i)]] (x) { x + i }
        Array.push(closures.&, f)
    }

    // Each closure should have captured a different value of i
    // closures[0] captures i=0, closures[1] captures i=1, etc.
    f0 := closures.data.[0]
    f1 := closures.data.[1]
    f2 := closures.data.[2]
    f3 := closures.data.[3]
    f4 := closures.data.[4]
    if f0(10) != 10 { error += 1 }
    if f1(10) != 11 { error += 1 }
    if f2(10) != 12 { error += 1 }
    if f3(10) != 13 { error += 1 }
    if f4(10) != 14 { error += 1 }

    Array.free(closures.&)
    error
}

// ---------------------------------------------------------------------------
// Helpers from test_hof_returned_closure.tl
// ---------------------------------------------------------------------------

returned_make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

make_predicate(threshold: Int) -> (Int) -> Bool {
    f := [[alloc, capture(threshold)]] (x) { x > threshold }
    f
}

make_comparator(ascending: Int) -> (Int, Int) -> Int {
    f := [[alloc, capture(ascending)]] (a, b) { (a - b) * ascending }
    f
}

// ---------------------------------------------------------------------------
// Tests from test_hof_returned_closure.tl
// ---------------------------------------------------------------------------

test_adder_map() {
    error := 0
    add10 := returned_make_adder(10)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, add10)
    if result.data.[0] != 11 { error += 1 }
    if result.data.[1] != 12 { error += 1 }
    if result.data.[2] != 13 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_predicate_filter() {
    error := 0
    gt3   := make_predicate(3)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, gt3)
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_comparator_sort() {
    error   := 0
    cmp_asc := make_comparator(1)
    arr     := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.sort(arr.&, cmp_asc)
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }

    cmp_desc := make_comparator(0 - 1)
    Array.sort(arr.&, cmp_desc)
    if arr.data.[0] != 3 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 1 { error += 1 }
    Array.free(arr.&)
    error
}

test_multiple_factories() {
    // Different factory closures used together
    error := 0
    add5  := returned_make_adder(5)
    gt10  := make_predicate(10)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 6)
    Array.push(arr.&, 9)
    // map +5 -> [8, 11, 14], then filter >10 -> [11, 14]
    mapped   := Array.map(arr, add5)
    filtered := Array.filter(mapped, gt10)
    if filtered.size != 2zu { error += 1 }
    if filtered.data.[0] != 11 { error += 1 }
    if filtered.data.[1] != 14 { error += 1 }
    Array.free(arr.&)
    Array.free(mapped.&)
    Array.free(filtered.&)
    error
}

// ---------------------------------------------------------------------------
// Tests from test_hof_stack_closure_capture.tl
// ---------------------------------------------------------------------------

test_map_capture() {
    error       := 0
    offset: Int := 100
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, (x) { x + offset })
    if result.size != 3zu { error += 1 }
    if result.data.[0] != 101 { error += 1 }
    if result.data.[1] != 102 { error += 1 }
    if result.data.[2] != 103 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_filter_capture() {
    error          := 0
    threshold: Int := 3
    arr            := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, (x) { x > threshold })
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_reduce_capture() {
    error      := 0
    scale: Int := 10
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    sum := Array.reduce(arr, 0, (acc, x) { acc + x * scale })
    if sum != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_foreach_capture() {
    error := 0
    total := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    Array.foreach(arr, (x) { total = total + x })
    if total != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_any_of_capture() {
    error       := 0
    target: Int := 20
    arr         := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.any_of(arr, (x) { x == target }) { error += 1 }
    missing: Int := 99
    if Array.any_of(arr, (x) { x == missing }) { error += 1 }
    Array.free(arr.&)
    error
}

test_all_capture() {
    error      := 0
    bound: Int := 5
    arr        := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.all(arr, (x) { x > bound }) { error += 1 }
    high_bound: Int := 15
    if Array.all(arr, (x) { x > high_bound }) { error += 1 }
    Array.free(arr.&)
    error
}

test_find_capture() {
    error          := 0
    threshold: Int := 15
    arr            := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    when Array.find(arr, (x) { x > threshold }) {
        s: Some { if s.value != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_sort_capture() {
    error          := 0
    direction: Int := 1                 // 1 = ascending, -1 = descending
    arr            := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.sort(arr.&, (a, b) { (a - b) * direction })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }

    // Now descending
    direction = 0 - 1
    Array.sort(arr.&, (a, b) { (a - b) * direction })
    if arr.data.[0] != 3 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 1 { error += 1 }
    Array.free(arr.&)
    error
}

// ---------------------------------------------------------------------------
// Tests from test_hof_alloc_closure.tl
// ---------------------------------------------------------------------------

test_map_alloc() {
    error       := 0
    offset: Int := 100
    f           := [[alloc, capture(offset)]] (x) { x + offset }
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, f)
    if result.data.[0] != 101 { error += 1 }
    if result.data.[1] != 102 { error += 1 }
    if result.data.[2] != 103 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_filter_alloc() {
    error          := 0
    threshold: Int := 3
    pred           := [[alloc, capture(threshold)]] (x) { x > threshold }
    arr            := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, pred)
    if result.size != 2zu { error += 1 }
    if result.data.[0] != 4 { error += 1 }
    if result.data.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_reduce_alloc() {
    error      := 0
    scale: Int := 10
    f          := [[alloc, capture(scale)]] (acc, x) { acc + x * scale }
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    sum := Array.reduce(arr, 0, f)
    if sum != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_any_of_alloc() {
    error       := 0
    target: Int := 20
    pred        := [[alloc, capture(target)]] (x) { x == target }
    arr         := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.any_of(arr, pred) { error += 1 }
    Array.free(arr.&)
    error
}

test_all_alloc() {
    error      := 0
    bound: Int := 5
    pred       := [[alloc, capture(bound)]] (x) { x > bound }
    arr        := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.all(arr, pred) { error += 1 }
    Array.free(arr.&)
    error
}

test_find_alloc() {
    error          := 0
    threshold: Int := 15
    pred           := [[alloc, capture(threshold)]] (x) { x > threshold }
    arr            := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    when Array.find(arr, pred) {
        s: Some { if s.value != 1zu { error += 1 } }
        n: None { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_sort_alloc() {
    error          := 0
    direction: Int := 1
    cmp            := [[alloc, capture(direction)]] (a, b) { (a - b) * direction }
    arr            := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.sort(arr.&, cmp)
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    Array.free(arr.&)
    error
}

test_capture_by_value() {
    // Verify capture-by-value: mutate original after closure creation
    error       := 0
    offset: Int := 100
    f           := [[alloc, capture(offset)]] (x) { x + offset }
    offset = 999                        // mutate original — should NOT affect f
    arr := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    result := Array.map(arr, f)
    if result.data.[0] != 101 { error += 1 }
    if result.data.[1] != 102 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_explicit_allocator() {
    error       := 0
    a           := Alloc.context.default
    offset: Int := 50
    f           := [[alloc(a), capture(offset)]] (x) { x + offset }
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    result := Array.map(arr, f)
    if result.data.[0] != 51 { error += 1 }
    if result.data.[1] != 52 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

// ---------------------------------------------------------------------------
// Main — calls all test functions
// ---------------------------------------------------------------------------

main() {
    error: CInt := 0

    // test_hof_closure_nested
    error += test_closure_capturing_closure()
    error += test_alloc_closure_capturing_closure()
    error += test_nested_with_filter()
    error += test_nested_reduce()

    // test_hof_closure_compose
    error += test_filter_then_map()
    error += test_map_then_reduce()
    error += test_full_pipeline()
    error += test_pipeline_alloc_closures()
    error += test_sort_then_filter()

    // test_hof_closure_generic
    error += test_generic_hof_stack_closure()
    error += test_generic_hof_alloc_closure()
    error += test_count_matching_stack()
    error += test_count_matching_alloc()
    error += test_fold_right()

    // test_hof_closure_in_struct
    error += test_struct_map()
    error += test_struct_filter()
    error += test_struct_pipeline()
    error += test_multiple_structs()

    // test_hof_closure_loop_capture
    error += test_closure_loop_capture()

    // test_hof_returned_closure
    error += test_adder_map()
    error += test_predicate_filter()
    error += test_comparator_sort()
    error += test_multiple_factories()

    // test_hof_stack_closure_capture
    error += test_map_capture()
    error += test_filter_capture()
    error += test_reduce_capture()
    error += test_foreach_capture()
    error += test_any_of_capture()
    error += test_all_capture()
    error += test_find_capture()
    error += test_sort_capture()

    // test_hof_alloc_closure
    error += test_map_alloc()
    error += test_filter_alloc()
    error += test_reduce_alloc()
    error += test_any_of_alloc()
    error += test_all_alloc()
    error += test_find_alloc()
    error += test_sort_alloc()
    error += test_capture_by_value()
    error += test_explicit_allocator()

    error
}
