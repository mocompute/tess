#module main
#import <Array.tl>

// Test: bare Array[T] in function return type
make_array() -> Array[Int] {
    a := Array.empty[Int]()
    Array.push(a.&, 1)
    Array.push(a.&, 2)
    a
}

test_return_type() -> CInt {
    error: CInt := 0
    a           := make_array()
    if a.size != 2zu { error += 1 }
    if a.data.[0] != 1 { error += 1 }
    if a.data.[1] != 2 { error += 1 }
    Array.free(a.&)
    error
}

// Test: bare Array[T] in function parameter type
sum_array(a: Array[Int]) -> Int {
    total    := 0
    i: CSize := 0zu
    while i < a.size, i += 1zu { total += a.data.[i] }
    total
}

test_param_type() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 10)
    Array.push(a.&, 20)
    Array.push(a.&, 30)
    if sum_array(a) != 60 { error += 1 }
    Array.free(a.&)
    error
}

// Test: explicit Array.Array[T] still works alongside the alias
identity_array(a: Array.Array[Int]) -> Array.Array[Int] { a }

test_explicit_qualified() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 99)
    b := identity_array(a)
    if b.data.[0] != 99 { error += 1 }
    Array.free(b.&)
    error
}

// Test: bare and explicit are interchangeable
convert(a: Array[Int]) -> Array.Array[Int] { a }

test_interchangeable() -> CInt {
    error: CInt := 0
    a           := Array.empty[Int]()
    Array.push(a.&, 7)
    b := convert(a)
    if b.data.[0] != 7 { error += 1 }
    Array.free(b.&)
    error
}

main() -> CInt {
    error: CInt := 0
    error += test_return_type()
    error += test_param_type()
    error += test_explicit_qualified()
    error += test_interchangeable()
    error
}
