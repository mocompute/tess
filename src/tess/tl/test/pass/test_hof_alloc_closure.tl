#module main
#import <Array.tl>
#import <Alloc.tl>

// Allocated closures passed to stdlib Array HOFs.

test_map_alloc() {
    error      := 0
    offset: Int := 100
    f          := [[alloc, capture(offset)]] (x) { x + offset }
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, f)
    if result.v.[0] != 101 { error += 1 }
    if result.v.[1] != 102 { error += 1 }
    if result.v.[2] != 103 { error += 1 }
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
    if result.v.[0] != 4 { error += 1 }
    if result.v.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_reduce_alloc() {
    error        := 0
    scale: Int   := 10
    f            := [[alloc, capture(scale)]] (acc, x) { acc + x * scale }
    arr          := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    sum := Array.reduce(arr, 0, f)
    if sum != 60 { error += 1 }
    Array.free(arr.&)
    error
}

test_any_of_alloc() {
    error        := 0
    target: Int  := 20
    pred         := [[alloc, capture(target)]] (x) { x == target }
    arr          := Array.empty[Int]()
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)
    if !Array.any_of(arr, pred) { error += 1 }
    Array.free(arr.&)
    error
}

test_all_alloc() {
    error       := 0
    bound: Int  := 5
    pred        := [[alloc, capture(bound)]] (x) { x > bound }
    arr         := Array.empty[Int]()
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
        s: Some { if s.v != 1zu { error += 1 } }
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
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 3 { error += 1 }
    Array.free(arr.&)
    error
}

test_capture_by_value() {
    // Verify capture-by-value: mutate original after closure creation
    error      := 0
    offset: Int := 100
    f          := [[alloc, capture(offset)]] (x) { x + offset }
    offset = 999  // mutate original — should NOT affect f
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    result := Array.map(arr, f)
    if result.v.[0] != 101 { error += 1 }
    if result.v.[1] != 102 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_explicit_allocator() {
    error      := 0
    a          := Alloc.context.default
    offset: Int := 50
    f          := [[alloc(a), capture(offset)]] (x) { x + offset }
    arr        := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    result := Array.map(arr, f)
    if result.v.[0] != 51 { error += 1 }
    if result.v.[1] != 52 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

main() {
    error: CInt := 0
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
