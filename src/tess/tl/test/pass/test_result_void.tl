#module main

// Helpers
make_ok_void() -> Result[Void, CInt] {
    Ok(void)
}

make_err_void(code: CInt) -> Result[Void, CInt] {
    Err(code)
}

// Test 1: Ok(void) construction and pattern match
test_ok_match() {
    r: Result[Void, CInt] := Ok(void)
    when r {
        ok:  Ok { 0 }
        err: Err { 1 }
    }
}

// Test 2: Err construction and pattern match
test_err_match() {
    r: Result[Void, CInt] := Err(42)
    when r {
        ok:  Ok { 1 }
        err: Err {
            if err.v == 42 { 0 }
            else { 1 }
        }
    }
}

// Test 3: try with Result[Void, CInt] — Ok case
test_try_ok() -> Result[Void, CInt] {
    try make_ok_void()
    Ok(void)
}

// Test 4: try with Result[Void, CInt] — Err propagation
test_try_err() -> Result[Void, CInt] {
    try make_err_void(99)
    Ok(void)
}

// Test 5: let-else with Result[Void, CInt]
test_let_else() {
    r       := make_ok_void()
    _ok: Ok := r else { return 1 }
    0
}

main() {
    fails := 0

    fails += test_ok_match()
    fails += test_err_match()

    // Test 3: try Ok — should succeed
    when test_try_ok() {
        ok:  Ok { fails += 0 }
        err: Err {
            c_printf("FAIL test_try_ok: expected Ok\n")
            fails += 1
        }
    }

    // Test 4: try Err — should propagate error
    when test_try_err() {
        ok: Ok {
            c_printf("FAIL test_try_err: expected Err\n")
            fails += 1
        }
        err: Err {
            if err.v != 99 {
                c_printf("FAIL test_try_err: wrong error value %d\n", err.v)
                fails += 1
            }
        }
    }

    fails += test_let_else()

    if fails == 0 { c_printf("All Result[Void] tests passed.\n") }
    else { c_printf("%d Result[Void] test(s) failed.\n", fails) }

    fails
}
