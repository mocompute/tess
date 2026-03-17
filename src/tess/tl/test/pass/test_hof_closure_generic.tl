#module main
#import <Array.tl>
#import <Alloc.tl>

// Custom generic HOFs accepting both stack and allocated closures.

apply_to_each[T](arr: Array[T], f: (T) -> T) -> Array[T] {
    result   := Array.empty[T]()
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        Array.push(result.&, f(arr.v.[i]))
    }
    result
}

count_matching[T](arr: Array[T], pred: (T) -> Bool) -> Int {
    n: Int   := 0
    i: CSize := 0zu
    while i < arr.size, i += 1zu {
        if pred(arr.v.[i]) { n += 1 }
    }
    n
}

fold_right[T, U](arr: Array[T], init: U, f: (T, U) -> U) -> U {
    acc := init
    i   := arr.size
    while i > 0zu {
        i   = i - 1zu
        acc = f(arr.v.[i], acc)
    }
    acc
}

test_generic_hof_stack_closure() {
    error       := 0
    offset: Int := 10
    arr         := Array.empty[Int]()
    Array.push(arr.&, 1)
    Array.push(arr.&, 2)
    Array.push(arr.&, 3)
    result := apply_to_each(arr, (x) { x + offset })
    if result.v.[0] != 11 { error += 1 }
    if result.v.[1] != 12 { error += 1 }
    if result.v.[2] != 13 { error += 1 }
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
    if result.v.[0] != 11 { error += 1 }
    if result.v.[1] != 12 { error += 1 }
    if result.v.[2] != 13 { error += 1 }
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

main() {
    error: CInt := 0
    error += test_generic_hof_stack_closure()
    error += test_generic_hof_alloc_closure()
    error += test_count_matching_stack()
    error += test_count_matching_alloc()
    error += test_fold_right()
    error
}
