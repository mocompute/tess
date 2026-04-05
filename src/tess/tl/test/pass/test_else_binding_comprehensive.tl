// Comprehensive test for else binding on tagged unions.
// Combines: test_else_binding_basic, test_else_binding_conditional,
//           test_else_binding_non_diverge, test_else_binding_nullary.

#module EB_MyRes

MyResult[T, E]: | MyOk { value: T } | MyErr { error: E }

#module EB_MyOpt

MyOption[T]: | MySome { value: T } | MyNone

#module EB_Pair

Pair: | Left { a: CInt, b: CInt } | Right { x: CInt, y: CInt, z: CInt }

#module main

// ---------------------------------------------------------------------------
// From test_else_binding_basic
// ---------------------------------------------------------------------------

// Test 1: else binding on Result — diverging (Ok case)
test_ok_diverge() -> CInt {
    val: EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyOk(42)
    ok:  MyOk                          := val else err { return err.error }
    ok.value - 42
}

// Test 2: else binding on Result — diverging (Err case)
test_err_diverge() -> CInt {
    val: EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(99)
    ok:  MyOk                          := val else err { return err.error - 99 }
    // Should not reach here
    1
}

// Test 3: else binding on Option — diverging (Some case)
test_some_diverge() -> CInt {
    val: EB_MyOpt.MyOption[CInt] := EB_MyOpt.MySome(7)
    s:   MySome                  := val else n { return 1 }
    s.value - 7
}

// Test 4: else binding on Option — diverging (None case)
test_none_diverge() -> CInt {
    val: EB_MyOpt.MyOption[CInt] := EB_MyOpt.MyNone()
    s:   MySome                  := val else n { return 0 }
    // Should not reach here
    1
}

// Test 5: else binding — access multiple fields
test_err_multi_field() -> CInt {
    error: CInt         := 0
    val:   EB_Pair.Pair := EB_Pair.Right(x = 10, y = 20, z = 30)
    l:     Left         := val else r {
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
    val: EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(5)
    ok:  MyOk                          := val else { return 0 }
    // Should not reach here
    1
}

test_basic() -> CInt {
    error: CInt := 0
    error += test_ok_diverge()
    error += test_err_diverge()
    error += test_some_diverge()
    error += test_none_diverge()
    error += test_err_multi_field()
    error += test_no_binding()
    error
}

// ---------------------------------------------------------------------------
// From test_else_binding_conditional
// ---------------------------------------------------------------------------

// Test 1: if variant binding with else binding — match case
test_if_match() -> CInt {
    val:   EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyOk(42)
    if ok: MyOk                          := val {
        return ok.value - 42
    } else err {
        return err.error + 100
    }
}

// Test 2: if variant binding with else binding — no match case
test_if_no_match() -> CInt {
    val:   EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(7)
    if ok: MyOk                          := val {
        return ok.value + 100
    } else err {
        return err.error - 7
    }
}

// Test 3: if variant binding without else binding still works
test_if_no_binding() -> CInt {
    val:   EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(3)
    if ok: MyOk                          := val {
        return 1
    } else {
        return 0
    }
}

// Test 4: if variant binding — no else at all still works
test_if_no_else() -> CInt {
    val:   EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyOk(5)
    if ok: MyOk                          := val {
        return ok.value - 5
    }
    1
}

// Test 5: else binding field access in conditional
test_if_err_fields() -> CInt {
    val:   EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(33)
    if ok: MyOk                          := val {
        return 1
    } else err {
        return err.error - 33
    }
}

test_conditional() -> CInt {
    error: CInt := 0
    error += test_if_match()
    error += test_if_no_match()
    error += test_if_no_binding()
    error += test_if_no_else()
    error += test_if_err_fields()
    error
}

// ---------------------------------------------------------------------------
// From test_else_binding_non_diverge
// ---------------------------------------------------------------------------

// Test 1: non-diverging else binding — match case (else not taken)
test_match() -> CInt {
    val: EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyOk(10)
    ok:  MyOk                          := val else err { err.error + 100 }
    ok.value - 10
}

// Test 2: non-diverging else binding — no match (else produces value)
test_no_match() -> CInt {
    val: EB_MyRes.MyResult[CInt, CInt] := EB_MyRes.MyErr(5)
    ok:  MyOk                          := val else err { err.error - 5 }
    // When else arm produces a value, the whole expression evaluates to it
    0
}

test_non_diverge() -> CInt {
    error: CInt := 0
    error += test_match()
    error += test_no_match()
    error
}

// ---------------------------------------------------------------------------
// From test_else_binding_nullary
// ---------------------------------------------------------------------------

// Test 1: else binding on nullary variant (MyNone has no fields)
test_nullary_match() -> CInt {
    val: EB_MyOpt.MyOption[CInt] := EB_MyOpt.MySome(42)
    s:   MySome                  := val else n { return 1 }
    s.value - 42
}

// Test 2: else binding on nullary variant — none case
test_nullary_none() -> CInt {
    val: EB_MyOpt.MyOption[CInt] := EB_MyOpt.MyNone()
    s:   MySome                  := val else n { return 0 }
    // Should not reach here
    1
}

// Test 3: primary variant is nullary, else gets the one with fields
test_primary_nullary() -> CInt {
    val: EB_MyOpt.MyOption[CInt] := EB_MyOpt.MySome(7)
    n:   MyNone                  := val else s { return s.value - 7 }
    // Should not reach here (val is MySome, so else runs)
    1
}

test_nullary() -> CInt {
    error: CInt := 0
    error += test_nullary_match()
    error += test_nullary_none()
    error += test_primary_nullary()
    error
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

main() {
    error: CInt := 0
    error += test_basic()
    error += test_conditional()
    error += test_non_diverge()
    error += test_nullary()
    error
}
