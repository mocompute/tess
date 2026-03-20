#module main
#import <Array.tl>
#import <Alloc.tl>

// Chaining multiple HOF calls with closures and composing closures.

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
    if result.v.[0] != 103 { error += 1 }
    if result.v.[1] != 104 { error += 1 }
    if result.v.[2] != 105 { error += 1 }
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
    if result.v.[0] != 4 { error += 1 }
    if result.v.[1] != 5 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

main() {
    error: CInt := 0
    error += test_filter_then_map()
    error += test_map_then_reduce()
    error += test_full_pipeline()
    error += test_pipeline_alloc_closures()
    error += test_sort_then_filter()
    error
}
