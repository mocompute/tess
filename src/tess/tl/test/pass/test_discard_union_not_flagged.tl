#module main

// Three-variant union — discard detection only applies to exactly-two-variant unions
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | Triangle

make_shape() -> Shape {
    Circle(1.0)
}

make_result() -> Result[Int, Int] {
    Ok(42)
}

make_option() -> Option[Int] {
    Some(1)
}

// Test 1: three-variant union in stmt position is fine
test_three_variant() -> CInt {
    make_shape()
    0
}

// Test 2: Result consumed by void-else is fine (void_else type is Void)
test_void_else() -> CInt {
    make_result() else err { return 1 }
    0
}

// Test 3: Result consumed by when is fine (when type is Void here)
test_when() -> CInt {
    when make_result() {
        o: Ok { }
        e: Err { }
    }
    0
}

// Test 4: Result as the return value (last expression) is fine — it is not discarded
test_return_value() -> Result[Int, Int] {
    make_result()
}

// Test 5: let binding is fine
test_let_binding() -> CInt {
    x := make_result()
    0
}

// Test 6: Option consumed by void-else is fine
test_option_void_else() -> CInt {
    make_option() else _ { return 1 }
    0
}

main() {
    error := 0
    error += test_three_variant()
    error += test_void_else()
    error += test_when()
    r := test_return_value()
    error += test_let_binding()
    error += test_option_void_else()
    error
}
