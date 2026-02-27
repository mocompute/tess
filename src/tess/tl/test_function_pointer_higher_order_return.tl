#module main

// Test: Returning function pointers from case expressions and higher-order functions
// Tests pattern matching that selects and returns different function pointers

double (x: Int) -> Int { x * 2 }
triple (x: Int) -> Int { x * 3 }
negate (x: Int) -> Int { 0 - x }
add_one(x: Int) -> Int { x + 1 }

// Tagged union for selecting operation
Op: | Double
    | Triple
    | Negate
    | AddOne

// Function that returns a function pointer based on tagged union variant
// Uses type inference for return type
get_operation(op: Op) {
    when op {
        d: Double { double/1 }
        t: Triple { triple/1 }
        n: Negate { negate/1 }
        a: AddOne { add_one/1 }
    }
}

// Function that conditionally returns one of two function pointers
select_fn(use_first: Bool, f1, f2) {
    if use_first { f1 } else { f2 }
}

main() {
    error := 0

    // Test 1: Get double operation and apply
    op1     := Double()
    fn1     := get_operation(op1)
    result1 := fn1(5)
    error += if result1 == 10 {0} else {1}

    // Test 2: Get triple operation and apply
    op2     := Triple()
    fn2     := get_operation(op2)
    result2 := fn2(5)
    error += if result2 == 15 {0} else {1}

    // Test 3: Get negate operation and apply
    op3     := Negate()
    fn3     := get_operation(op3)
    result3 := fn3(7)
    error += if result3 == -7 {0} else {1}

    // Test 4: Get add_one operation and apply
    op4     := AddOne()
    fn4     := get_operation(op4)
    result4 := fn4(10)
    error += if result4 == 11 {0} else {1}

    // Test 5: Select between two function pointers (true case)
    fn5     := select_fn(true, double/1, triple/1)
    result5 := fn5(4)
    error += if result5 == 8 {0} else {1}

    // Test 6: Select between two function pointers (false case)
    fn6     := select_fn(false, double/1, triple/1)
    result6 := fn6(4)
    error += if result6 == 12 {0} else {1}

    // Test 7: Chain get_operation calls
    op_a := Triple()
    op_b := AddOne()
    fn_a := get_operation(op_a)
    fn_b := get_operation(op_b)
    // Apply both: triple(2) + add_one(2) = 6 + 3 = 9
    result7 := fn_a(2) + fn_b(2)
    error += if result7 == 9 {0} else {1}

    error
}
