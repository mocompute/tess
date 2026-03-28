#module main
#import <Array.tl>

cmp_asc(a, b) { a - b }

test_empty() {
    error := 0
    arr   := Array.empty[Int]()
    Array.sort(arr.&, (a, b) { a - b })
    if arr.size != 0zu { error += 1 }
    error
}

test_single() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 42)
    Array.sort(arr.&, (a, b) { a - b })
    if arr.size != 1zu { error += 1 }
    if arr.data.[0] != 42 { error += 1 }
    Array.free(arr.&)
    error
}

test_ascending() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 5)
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 4)
    Array.push(arr.&, 2)
    Array.sort(arr.&, (a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 4 { error += 1 }
    if arr.data.[4] != 5 { error += 1 }
    Array.free(arr.&)
    error
}

test_descending() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    Array.sort(arr.&, (a, b) { b - a })
    if arr.data.[0] != 5 { error += 1 }
    if arr.data.[1] != 4 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 2 { error += 1 }
    if arr.data.[4] != 1 { error += 1 }
    Array.free(arr.&)
    error
}

test_named_comparator() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.sort(arr.&, cmp_asc/2)
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    Array.free(arr.&)
    error
}

test_sorted() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    result := Array.sorted(arr, (a, b) { a - b })
    // Result is sorted
    if result.data.[0] != 1 { error += 1 }
    if result.data.[1] != 2 { error += 1 }
    if result.data.[2] != 3 { error += 1 }
    // Original unchanged
    if arr.data.[0] != 3 { error += 1 }
    if arr.data.[1] != 1 { error += 1 }
    if arr.data.[2] != 2 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_already_sorted() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.sort(arr.&, (a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 4 { error += 1 }
    Array.free(arr.&)
    error
}

test_sort_with_allocator() {
    error := 0
    arr   := Array.empty[Int]()
    Array.push(arr.&, 5)
    Array.push(arr.&, 3)
    Array.push(arr.&, 1)
    Array.sort(arr.&, Alloc.context.transient, (a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 3 { error += 1 }
    if arr.data.[2] != 5 { error += 1 }
    Array.free(arr.&)
    error
}

main() {
    error := 0
    error += test_empty()
    error += test_single()
    error += test_ascending()
    error += test_descending()
    error += test_named_comparator()
    error += test_sorted()
    error += test_already_sorted()
    error += test_sort_with_allocator()
    error
}
