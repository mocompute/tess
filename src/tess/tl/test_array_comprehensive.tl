#module main
#import <Array.tl>

// Helper struct for testing with complex types
Point[a]: { x: a, y: a }

test_init_basic() {
    error := 0
    arr   := Array.init(42, 5u)
    if arr.size != 5u { error += 1 }
    for x in arr {
        if x != 42 { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_init_zero_count() {
    error := 0
    arr   := Array.init(0, 0u)
    if arr.size != 0u { error += 1 }
    Array.free(arr.&)
    error
}

test_init_struct() {
    error := 0
    arr   := Array.init(Point(x = 1, y = 2), 3u)
    if arr.size != 3u { error += 1 }
    for p in arr {
        if p.x != 1 { error += 1 }
        if p.y != 2 { error += 1 }
    }
    Array.free(arr.&)
    error
}

test_with_capacity_undefined_basic() {
    error := 0
    arr   := Array.with_capacity_undefined[Int](10u)
    if arr.size != 10u { error += 1 }
    if arr.capacity != 10u { error += 1 }
    // Write values and read them back
    i := 0
    while i < 10 {
        arr.v.[i] = i * 10
        i         = i + 1
    }
    i = 0
    while i < 10 {
        if arr.v.[i] != i * 10 { error += 1 }
        i = i + 1
    }
    Array.free(arr.&)
    error
}

test_reserve_empty() {
    error := 0
    arr   := Array.empty[Int]()
    Array.reserve(arr.&, 10u)
    if arr.capacity < 10u { error += 1 }
    if arr.size != 0u { error += 1 }
    Array.free(arr.&)
    error
}

test_reserve_growth() {
    error := 0
    arr   := Array.with_capacity[Int](2u)
    Array.push(arr.&, 111)
    Array.push(arr.&, 222)
    Array.reserve(arr.&, 100u)
    if arr.capacity < 100u { error += 1 }
    if arr.size != 2u { error += 1 }
    // Verify elements are preserved
    if arr.v.[0] != 111 { error += 1 }
    if arr.v.[1] != 222 { error += 1 }
    Array.free(arr.&)
    error
}

test_reserve_noop() {
    error             := 0
    arr               := Array.with_capacity[Int](50u)
    original_capacity := arr.capacity
    Array.reserve(arr.&, 10u)
    // reserve is no-op when capacity is already sufficient
    if arr.capacity != original_capacity { error += 1 }
    Array.free(arr.&)
    error
}

test_push_growth() {
    error := 0
    arr   := Array.with_capacity[Int](2u)
    Array.push(arr.&, 10)
    Array.push(arr.&, 20)
    Array.push(arr.&, 30)               // Forces growth
    if arr.size != 3u { error += 1 }
    if arr.capacity < 3u { error += 1 }
    if arr.v.[0] != 10 { error += 1 }
    if arr.v.[1] != 20 { error += 1 }
    if arr.v.[2] != 30 { error += 1 }
    Array.free(arr.&)
    error
}

test_push_many() {
    error := 0
    arr   := Array.empty[Int]()
    i     := 0
    while i < 100 {
        Array.push(arr.&, i)
        i = i + 1
    }
    if arr.size != 100u { error += 1 }
    i = 0
    while i < 100 {
        if arr.v.[i] != i { error += 1 }
        i = i + 1
    }
    Array.free(arr.&)
    error
}

test_iter_empty() {
    error := 0
    arr   := Array.empty[Int]()
    count := 0
    for x in arr {
        count = count + 1
    }
    if count != 0 { error += 1 }
    Array.free(arr.&)
    error
}

test_iter_single() {
    error := 0
    arr   := Array.init(42, 1u)
    count := 0
    for x in arr {
        if x != 42 { error += 1 }
        count = count + 1
    }
    if count != 1 { error += 1 }
    Array.free(arr.&)
    error
}

test_iter_mutable() {
    error := 0
    arr   := Array.init(0, 5u)
    // Use mutable iterator to set each element to its position
    pos := 0
    for x.& in arr {
        x.* = pos
        pos = pos + 1
    }
    // Verify elements are 0, 1, 2, 3, 4
    i := 0
    while i < 5 {
        if arr.v.[i] != i { error += 1 }
        i = i + 1
    }
    Array.free(arr.&)
    error
}

test_capacity_boundary() {
    error := 0
    arr   := Array.with_capacity[Int](4u)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    // Filled exactly to capacity
    if arr.size != 4u { error += 1 }
    if arr.capacity != 4u { error += 1 }
    // Push 5th element, triggers growth
    Array.push(arr.&, 5)
    if arr.size != 5u { error += 1 }
    if arr.capacity <= 4u { error += 1 }
    Array.free(arr.&)
    error
}

test_zero_capacity() {
    error := 0
    arr   := Array.with_capacity[Int](0u)
    if arr.capacity != 0u { error += 1 }
    Array.push(arr.&, 99)
    if arr.size != 1u { error += 1 }
    if arr.capacity < 1u { error += 1 }
    if arr.v.[0] != 99 { error += 1 }
    Array.free(arr.&)
    error
}

test_indexed_iterator() {
    error := 0
    arr   := Array.with_capacity_undefined[UInt](4u)
    // Use Array.Indexed to set values: ptr.* = index
    for it.& in Array.Indexed arr {
        it.ptr.* = it.index
    }
    // Verify elements are 0, 1, 2, 3
    i := 0u
    while i < 4u {
        if arr.v.[i] != i { error += 1 }
        i = i + 1u
    }
    Array.free(arr.&)
    error
}

main() {
    error := 0
    error += test_init_basic()
    error += test_init_zero_count()
    error += test_init_struct()
    error += test_with_capacity_undefined_basic()
    error += test_reserve_empty()
    error += test_reserve_growth()
    error += test_reserve_noop()
    error += test_push_growth()
    error += test_push_many()
    error += test_iter_empty()
    error += test_iter_single()
    error += test_iter_mutable()
    error += test_capacity_boundary()
    error += test_zero_capacity()
    error += test_indexed_iterator()
    error
}
