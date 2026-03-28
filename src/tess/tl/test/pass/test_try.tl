#module MyRes

MyResult[T, E]: | MyOk { value: T } | MyErr { error: E }

#module MyOpt

MyOption[T]: | MySome { value: T } | MyNone

#module main

// Helper: returns MyOk with value
make_ok(x) -> MyRes.MyResult[Int, Int] {
    MyRes.MyOk(x)
}

// Helper: returns MyErr with value
make_err(x) -> MyRes.MyResult[Int, Int] {
    MyRes.MyErr(x)
}

// Test 1: try unwraps MyOk variant to inner value
test_try_ok() -> MyRes.MyResult[Int, Int] {
    val := make_ok(42)
    s   := try val
    MyRes.MyOk(s)
}

// Test 2: try propagates MyErr
test_try_err() -> MyRes.MyResult[Int, Int] {
    val := make_err(99)
    s   := try val
    MyRes.MyOk(s + 1)                   // should not reach
}

// Test 3: try composes inline
test_try_compose() -> MyRes.MyResult[Int, Int] {
    s := try make_ok(10)
    MyRes.MyOk(s)
}

// Test 4: try with MyOption - unwraps MySome
test_try_option_some() -> MyOpt.MyOption[Int] {
    val := MyOpt.MySome(7)
    s   := try val
    MyOpt.MySome(s + 1)
}

// Test 5: try with MyOption - propagates MyNone
test_try_option_none() -> MyOpt.MyOption[Int] {
    val: MyOpt.MyOption[Int] := MyOpt.MyNone()
    s                        := try val
    MyOpt.MySome(s + 1)                 // should not reach
}

// Test 6: nested try
test_try_nested() -> MyRes.MyResult[Int, Int] {
    inner := try make_ok(5)
    s     := try make_ok(inner)
    MyRes.MyOk(s)
}

// Test 7: try with defer (defer runs on early return)
test_try_defer_inner() -> MyRes.MyResult[Int, Int] {
    x := 0
    defer x = 1
    val := make_err(42)
    s   := try val
    MyRes.MyOk(s)
}

main() {
    error := 0

    // Test 1: try unwraps MyOk
    r1 := test_try_ok()
    when r1 {
        s: MyOk { error += if s.value == 42 { 0 } else { 1 } }
        e: MyErr { error += 1 }
    }

    // Test 2: try propagates MyErr
    r2 := test_try_err()
    when r2 {
        s: MyOk { error += 1 }
        e: MyErr { error += if e.error == 99 { 0 } else { 1 } }
    }

    // Test 3: try composes inline
    r3 := test_try_compose()
    when r3 {
        s: MyOk { error += if s.value == 10 { 0 } else { 1 } }
        e: MyErr { error += 1 }
    }

    // Test 4: try with MyOption - unwraps MySome
    r4 := test_try_option_some()
    when r4 {
        s: MySome { error += if s.value == 8 { 0 } else { 1 } }
        n: MyNone { error += 1 }
    }

    // Test 5: try with MyOption - propagates MyNone
    r5 := test_try_option_none()
    when r5 {
        s: MySome { error += 1 }
        n: MyNone { error += 0 }
    }

    // Test 6: nested try
    r6 := test_try_nested()
    when r6 {
        s: MyOk { error += if s.value == 5 { 0 } else { 1 } }
        e: MyErr { error += 1 }
    }

    // Test 7: try with defer - should compile and early-return MyErr(42)
    r7 := test_try_defer_inner()
    when r7 {
        s: MyOk { error += 1 }
        e: MyErr { error += if e.error == 42 { 0 } else { 1 } }
    }

    error
}
