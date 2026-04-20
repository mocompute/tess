#import <Array.tl>

#module Tuple

Tuple: { x1: Int, x2: Int }

// Ord conformance for Tuple:
cmp(a: Ptr[Const[Tuple]], b: Ptr[Const[Tuple]]) -> CInt {
    if a->x1 < b->x1 { -1 }
    else if a->x1 > b->x1 { 1 }
    else {
        if a->x2 < b->x2 { -1 }
        else if a->x2 > b->x2 { 1 }
        else { 0 }
    }
}

#module main

cmp_asc(a, b) { a - b }

test_empty() {
    error := 0
    arr   := Array.empty[Int]()
    arr.sort_with((a, b) { a - b })
    if arr.size != 0zu { error += 1 }
    error
}

test_single() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(42)
    arr.sort_with((a, b) { a - b })
    if arr.size != 1zu { error += 1 }
    if arr.data.[0] != 42 { error += 1 }
    error
}

test_ascending() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(5)
    arr.push(3)
    arr.push(1)
    arr.push(4)
    arr.push(2)
    arr.sort_with((a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 4 { error += 1 }
    if arr.data.[4] != 5 { error += 1 }
    error
}

test_descending() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(1)
    arr.push(2)
    arr.push(3)
    arr.push(4)
    arr.push(5)
    arr.sort_with((a, b) { b - a })
    if arr.data.[0] != 5 { error += 1 }
    if arr.data.[1] != 4 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 2 { error += 1 }
    if arr.data.[4] != 1 { error += 1 }
    error
}

test_named_comparator() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(3)
    arr.push(1)
    arr.push(2)
    arr.sort_with(cmp_asc/2)
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    error
}

test_sorted() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(3)
    arr.push(1)
    arr.push(2)
    result := arr.sorted_with((a, b) { a - b })
    defer result.free()
    // Result is sorted
    if result.data.[0] != 1 { error += 1 }
    if result.data.[1] != 2 { error += 1 }
    if result.data.[2] != 3 { error += 1 }
    // Original unchanged
    if arr.data.[0] != 3 { error += 1 }
    if arr.data.[1] != 1 { error += 1 }
    if arr.data.[2] != 2 { error += 1 }
    error
}

test_already_sorted() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(1)
    arr.push(2)
    arr.push(3)
    arr.push(4)
    arr.sort_with((a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 2 { error += 1 }
    if arr.data.[2] != 3 { error += 1 }
    if arr.data.[3] != 4 { error += 1 }
    error
}

test_sort_with_allocator() {
    error := 0
    arr   := Array.empty[Int]()
    defer arr.free()
    arr.push(5)
    arr.push(3)
    arr.push(1)
    arr.sort_with(Alloc.context.transient, (a, b) { a - b })
    if arr.data.[0] != 1 { error += 1 }
    if arr.data.[1] != 3 { error += 1 }
    if arr.data.[2] != 5 { error += 1 }
    error
}

test_sort_ord() {
    error := 0

    arr := Array.empty[Tuple]()
    defer arr.free()
    arr.push(Tuple(x1 = 3, x2 = 3))
    arr.push(Tuple(x1 = 2, x2 = 2))
    arr.push(Tuple(x1 = 1, x2 = 1))
    arr.sort()
    if arr.data.[0].x1 != 1 { error += 1 }
    if arr.data.[1].x1 != 2 { error += 1 }
    if arr.data.[2].x1 != 3 { error += 1 }
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
    error += test_sort_ord()

    error
}
