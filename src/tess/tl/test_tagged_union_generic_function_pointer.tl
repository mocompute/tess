#module main

// Test: Generic tagged union where one variant stores function pointer
// Using a fixed function signature since generic function signatures
// in structs have compiler limitations

double_int(x: Int) -> Int { x * 2 }
negate    (x: Int) -> Int { 0 - x }

// Struct with function pointer (non-generic function signature)
IntHandler: { fn: (Int) -> Int }

// Generic tagged union with a variant that contains function pointer struct
Result(T): | Ok { value: T }
           | Err { code: Int, handler: IntHandler }

           // Helper function to apply handler
apply_int_handler(h: IntHandler, x: Int) -> Int {
    h.fn(x)
}

main() {
    error := 0

    // Test 1: Ok variant with Int value
    ok_int  := Result.Ok(value = 42)
    result1 := case ok_int: Result {
        o: Ok { o.value }
        e: Err { apply_int_handler(e.handler, e.code) }
    }
    error += if result1 == 42 {0} else {1}

    // Test 2: Ok variant with Float value
    ok_float := Result.Ok(value = 3.14)
    result2  := case ok_float: Result {
        o: Ok { o.value }
        e: Err { 0.0 }
    }
    error += if result2 == 3.14 {0} else {1}

    // Test 3: Err variant with double handler
    h1                                := IntHandler(fn = double_int/1)
    err1:                 Result(Int) := Result.Err(code = 5, handler = h1)
    result3                           := case err1: Result {
        o: Ok { 0 }
        e: Err { apply_int_handler(e.handler, e.code) }
    }
    error += if result3 == 10 {0} else {1}

    // Test 4: Err variant with negate handler
    h2                                := IntHandler(fn = negate/1)
    err2:                 Result(Int) := Result.Err(code = 7, handler = h2)
    result4                           := case err2: Result {
        o: Ok { 0 }
        e: Err { apply_int_handler(e.handler, e.code) }
    }
    error += if result4 == -7 {0} else {1}

    error
}
