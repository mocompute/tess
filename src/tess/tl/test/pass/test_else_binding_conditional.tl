// Test else binding in conditional variant binding (if form).
#module MyRes

MyResult[T, E]: | MyOk { value: T } | MyErr { error: E }

#module main

// Test 1: if variant binding with else binding — match case
test_if_match() -> CInt {
    val:   MyRes.MyResult[CInt, CInt] := MyRes.MyOk(42)
    if ok: MyOk                       := val {
        return ok.value - 42
    } else err {
        return err.error + 100
    }
}

// Test 2: if variant binding with else binding — no match case
test_if_no_match() -> CInt {
    val:   MyRes.MyResult[CInt, CInt] := MyRes.MyErr(7)
    if ok: MyOk                       := val {
        return ok.value + 100
    } else err {
        return err.error - 7
    }
}

// Test 3: if variant binding without else binding still works
test_if_no_binding() -> CInt {
    val:   MyRes.MyResult[CInt, CInt] := MyRes.MyErr(3)
    if ok: MyOk                       := val {
        return 1
    } else {
        return 0
    }
}

// Test 4: if variant binding — no else at all still works
test_if_no_else() -> CInt {
    val:   MyRes.MyResult[CInt, CInt] := MyRes.MyOk(5)
    if ok: MyOk                       := val {
        return ok.value - 5
    }
    1
}

// Test 5: else binding field access in conditional
test_if_err_fields() -> CInt {
    val:   MyRes.MyResult[CInt, CInt] := MyRes.MyErr(33)
    if ok: MyOk                       := val {
        return 1
    } else err {
        return err.error - 33
    }
}

main() {
    error: CInt := 0
    error += test_if_match()
    error += test_if_no_match()
    error += test_if_no_binding()
    error += test_if_no_else()
    error += test_if_err_fields()
    error
}
