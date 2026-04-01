// Test else binding on two-variant tagged unions.
#module MyRes

MyResult[T, E]: | MyOk { value: T } | MyErr { error: E }

#module MyOpt

MyOption[T]: | MySome { value: T } | MyNone

#module Pair

Pair: | Left { a: CInt, b: CInt } | Right { x: CInt, y: CInt, z: CInt }

#module main

// Test 1: else binding on Result — diverging (Ok case)
test_ok_diverge() -> CInt {
    val: MyRes.MyResult[CInt, CInt] := MyRes.MyOk(42)
    ok: MyOk := val else err { return err.error }
    ok.value - 42
}

// Test 2: else binding on Result — diverging (Err case)
test_err_diverge() -> CInt {
    val: MyRes.MyResult[CInt, CInt] := MyRes.MyErr(99)
    ok: MyOk := val else err { return err.error - 99 }
    // Should not reach here
    1
}

// Test 3: else binding on Option — diverging (Some case)
test_some_diverge() -> CInt {
    val: MyOpt.MyOption[CInt] := MyOpt.MySome(7)
    s: MySome := val else n { return 1 }
    s.value - 7
}

// Test 4: else binding on Option — diverging (None case)
test_none_diverge() -> CInt {
    val: MyOpt.MyOption[CInt] := MyOpt.MyNone()
    s: MySome := val else n { return 0 }
    // Should not reach here
    1
}

// Test 5: else binding — access multiple fields
test_err_multi_field() -> CInt {
    error: CInt := 0
    val: Pair.Pair := Pair.Right(x = 10, y = 20, z = 30)
    l: Left := val else r {
        if r.x != 10 { error += 1 }
        if r.y != 20 { error += 1 }
        if r.z != 30 { error += 1 }
        return error
    }
    // Should not reach here
    1
}

// Test 6: plain else (no binding) still works
test_no_binding() -> CInt {
    val: MyRes.MyResult[CInt, CInt] := MyRes.MyErr(5)
    ok: MyOk := val else { return 0 }
    // Should not reach here
    1
}

main() {
    error: CInt := 0
    error += test_ok_diverge()
    error += test_err_diverge()
    error += test_some_diverge()
    error += test_none_diverge()
    error += test_err_multi_field()
    error += test_no_binding()
    error
}
