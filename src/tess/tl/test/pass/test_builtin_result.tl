#module main

// Test the builtin Result[T, U] type with when expressions

// Generic function using when on Result
get_or_default[T, U](res: Result[T, U], default: T) -> T {
    when res {
        o: Ok { o.v }
        e: Err { default }
    }
}

// Generic function using when on Result, accessing the error
get_err_or_default[T, U](res: Result[T, U], default: U) -> U {
    when res {
        o: Ok { default }
        e: Err { e.v }
    }
}

main() {
    error := 0

    // Test Ok variant (both type params must be constrained)
    good: Result[Int, Int] := Ok(42)
    result                 := when good {
        o: Ok { o.v }
        e: Err { 0 }
    }
    error += if result == 42 {0} else {1}

    // Test Err variant
    bad: Result[Int, Int] := Err(99)
    result                := when bad {
        o: Ok { 0 }
        e: Err { e.v }
    }
    error += if result == 99 {0} else {1}

    // Test with Float types
    fgood: Result[Float, Int] := Ok(3.14)
    result_f                  := when fgood {
        o: Ok { o.v }
        e: Err { 0.0 }
    }
    error += if result_f == 3.14 {0} else {1}

    // Test generic function with Ok
    ok_val: Result[Int, Int] := Ok(10)
    error += if get_or_default(ok_val, 0) == 10 {0} else {1}

    // Test generic function with Err
    err_val: Result[Int, Int] := Err(5)
    error += if get_or_default(err_val, 77) == 77 {0} else {1}

    // Test get_err_or_default
    error += if get_err_or_default(err_val, 0) == 5 {0} else {1}

    // Test with else arm
    ok_val2: Result[Int, Int] := Ok(100)
    result                    := when ok_val2 {
        o: Ok { o.v }
        else { 0 }
    }
    error += if result == 100 {0} else {1}

    error
}
