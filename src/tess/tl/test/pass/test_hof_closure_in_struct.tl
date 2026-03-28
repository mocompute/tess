#module main
#import <Array.tl>
#import <Alloc.tl>

// Allocated closures stored in struct fields, extracted and passed to HOFs.

Transformer: { transform: (Int) -> Int, predicate: (Int) -> Bool }

make_transformer(offset: Int, threshold: Int) -> Transformer {
    t := [[alloc, capture(offset)]] (x) { x + offset }
    p := [[alloc, capture(threshold)]] (x) { x > threshold }
    Transformer(transform = t, predicate = p)
}

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

main() {
    error: CInt := 0
    error += test_struct_map()
    error += test_struct_filter()
    error += test_struct_pipeline()
    error += test_multiple_structs()
    error
}
