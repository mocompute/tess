#module main

#import <cstdio.tl>

// Helper: returns Ok(void)
make_ok() -> Result[Void, CInt] {
    Ok(void)
}

// Helper: returns Err with value
make_err(x: CInt) -> Result[Void, CInt] {
    Err(x)
}

// Test 1: basic void-else on Ok (body not executed)
test_basic_ok() -> CInt {
    make_ok() else err { return 1 }
    0
}

// Test 2: basic void-else on Err (body executed, returns early)
test_basic_err() -> CInt {
    make_err(42) else err { return 0 }
    1                                   // should not reach here
}

// Test 3: multiple void-else in sequence
test_sequence() -> CInt {
    make_ok() else err { return 1 }
    make_ok() else err { return 2 }
    0
}

// Test 4: void-else in loop with break
test_loop_break() -> CInt {
    i:      CInt := 0
    result: CInt := 0
    while i < 3 {
        make_ok() else err {
            result = err.error
            break
        }
        i += 1
    }
    if i == 3 { 0 } else { 1 }
}

// Test 5: void-else with error field access
test_err_field() -> CInt {
    make_err(99) else err {
        return if err.error == 99 { 0 } else { 1 }
    }
    1
}

// Test 6: void-else with Option
test_option_some() -> CInt {
    Some(42) else err { return 1 }
    0
}

test_option_none() -> CInt {
    none: Option[CInt] := None()
    none else err { return 0 }
    1
}

// Test 7: void-else with non-void first variant (Result[CInt, CInt])
test_result_non_void() -> CInt {
    r: Result[CInt, CInt] := Ok(10)
    r else err { return 1 }
    0
}

main() {
    error := 0
    error += test_basic_ok()
    error += test_basic_err()
    error += test_sequence()
    error += test_loop_break()
    error += test_err_field()
    error += test_option_some()
    error += test_option_none()
    error += test_result_non_void()
    error
}
