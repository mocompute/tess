#module main

// Test: try across different Result types that share the same error type.
// e.g. try expr where expr : Result[A, E] inside a function returning Result[B, E]

make_ok_int(x: CInt) -> Result[CInt, CInt] {
    Ok(x)
}

make_err_int(x: CInt) -> Result[CInt, CInt] {
    Err(x)
}

_err(code: CInt) -> Result[Void, CInt] {
    Err(code)
}

// Cross-type try: Result[CInt, CInt] -> Result[Void, CInt]
test_cross_ok() -> Result[Void, CInt] {
    val := try make_ok_int(42)
    if val != 42 { return _err(-1) }
    Ok(void)
}

test_cross_err() -> Result[Void, CInt] {
    try make_err_int(99)
    Ok(void)
}

// Cross-type try: Result[CInt, CInt] -> Result[Int, CInt]
test_cross_ok_different_success() -> Result[Int, CInt] {
    val         := try make_ok_int(7)
    result: Int := val
    Ok(result)
}

// Multiple cross-type try in one function
test_multi_try() -> Result[Void, CInt] {
    a := try make_ok_int(10)
    b := try make_ok_int(20)
    if a + b != 30 { return _err(-2) }
    Ok(void)
}

// Cross-type try where error propagates through two levels
inner(x: CInt) -> Result[CInt, CInt] {
    if x < 0 { return Err(x) }
    Ok(x * 2)
}

outer(x: CInt) -> Result[Void, CInt] {
    val := try inner(x)
    if val != x * 2 { return _err(-3) }
    Ok(void)
}

main() {
    fails := 0

    // Test 1: cross-type try, Ok path
    when test_cross_ok() {
        ok:  Ok { fails += 0 }
        err: Err {
            c_printf(c"FAIL test_cross_ok: got Err(%d)\n", err.error)
            fails += 1
        }
    }

    // Test 2: cross-type try, Err path — error value preserved
    when test_cross_err() {
        ok: Ok {
            c_printf(c"FAIL test_cross_err: expected Err\n")
            fails += 1
        }
        err: Err {
            if err.error != 99 {
                c_printf(c"FAIL test_cross_err: expected 99, got %d\n", err.error)
                fails += 1
            }
        }
    }

    // Test 3: cross-type with different success types
    when test_cross_ok_different_success() {
        ok: Ok {
            if ok.value != 7 {
                c_printf(c"FAIL test_cross_ok_different_success: expected 7, got %d\n", ok.value)
                fails += 1
            }
        }
        err: Err {
            c_printf(c"FAIL test_cross_ok_different_success: got Err\n")
            fails += 1
        }
    }

    // Test 4: multiple cross-type try
    when test_multi_try() {
        ok:  Ok { fails += 0 }
        err: Err {
            c_printf(c"FAIL test_multi_try: got Err(%d)\n", err.error)
            fails += 1
        }
    }

    // Test 5: error propagates through cross-type try
    when outer(-5) {
        ok: Ok {
            c_printf(c"FAIL outer(-5): expected Err\n")
            fails += 1
        }
        err: Err {
            if err.error != -5 {
                c_printf(c"FAIL outer(-5): expected -5, got %d\n", err.error)
                fails += 1
            }
        }
    }

    when outer(10) {
        ok:  Ok { fails += 0 }
        err: Err {
            c_printf(c"FAIL outer(10): got Err(%d)\n", err.error)
            fails += 1
        }
    }

    if fails == 0 { c_printf(c"All cross-type try tests passed.\n") }
    else { c_printf(c"%d cross-type try test(s) failed.\n", fails) }

    fails
}
