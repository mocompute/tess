#module main
#import <Array.tl>
#import <Alloc.tl>

// Factory functions return allocated closures, callers pass them to HOFs.

make_adder(n: Int) -> (Int) -> Int {
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

test_adder_map() {
    error := 0
    add10 := make_adder(10)
    arr   := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := Array.map(arr, add10)
    if result.v.[0] != 11 { error += 1 }
    if result.v.[1] != 12 { error += 1 }
    if result.v.[2] != 13 { error += 1 }
    Array.free(arr.&)
    Array.free(result.&)
    error
}

test_predicate_filter() {
    error  := 0
    gt3    := make_predicate(3)
    arr    := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    Array.push(arr.&, 4)
    Array.push(arr.&, 5)
    result := Array.filter(arr, gt3)
    if result.size != 2zu { error += 1 }
    if result.v.[0] != 4 { error += 1 }
    if result.v.[1] != 5 { error += 1 }
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
    if arr.v.[0] != 1 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 3 { error += 1 }

    cmp_desc := make_comparator(0 - 1)
    Array.sort(arr.&, cmp_desc)
    if arr.v.[0] != 3 { error += 1 }
    if arr.v.[1] != 2 { error += 1 }
    if arr.v.[2] != 1 { error += 1 }
    Array.free(arr.&)
    error
}

test_multiple_factories() {
    // Different factory closures used together
    error  := 0
    add5   := make_adder(5)
    gt10   := make_predicate(10)
    arr    := Array.empty[Int]()
    Array.push(arr.&, 3)
    Array.push(arr.&, 6)
    Array.push(arr.&, 9)
    // map +5 -> [8, 11, 14], then filter >10 -> [11, 14]
    mapped   := Array.map(arr, add5)
    filtered := Array.filter(mapped, gt10)
    if filtered.size != 2zu { error += 1 }
    if filtered.v.[0] != 11 { error += 1 }
    if filtered.v.[1] != 14 { error += 1 }
    Array.free(arr.&)
    Array.free(mapped.&)
    Array.free(filtered.&)
    error
}

main() {
    error: CInt := 0
    error += test_adder_map()
    error += test_predicate_filter()
    error += test_comparator_sort()
    error += test_multiple_factories()
    error
}
