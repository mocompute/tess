#module main
#import <Array.tl>
#import <Alloc.tl>

// Closures capturing other closures, passed to HOFs.

make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

test_closure_capturing_closure() {
    error := 0
    add5  := make_adder(5)
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
    add10 := make_adder(10)
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
    add3  := make_adder(3)
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
    add1  := make_adder(1)
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

main() {
    error: CInt := 0
    error += test_closure_capturing_closure()
    error += test_alloc_closure_capturing_closure()
    error += test_nested_with_filter()
    error += test_nested_reduce()
    error
}
