#module main

// Test 1: when with diverging arm (return) + Void arm
test_when_return_void() {
    x: Option[CInt] := Some(42)
    when x {
        s: Some { return 0 }
        n: None { }
    }
    1
}

// Test 2: when with diverging arm (return) + value arm (CInt)
test_when_return_value() {
    x: Option[CInt] := Some(42)
    result          := when x {
        s: Some { s.v }
        n: None { return 1 }
    }
    if result != 42 { return 1 }
    0
}

// Test 3: if-else where one branch diverges
test_if_else_diverge() {
    x      := true
    result := if x { 42 }
    else { return 1 }
    if result != 42 { return 1 }
    0
}

// Test 4: if-else where the other branch diverges
test_if_else_diverge_reverse() {
    x      := false
    result := if x { return 1 }
    else { 99 }
    if result != 99 { return 1 }
    0
}

// Test 5: when with multiple arms, one diverges
test_when_multi_diverge() {
    x: Result[CInt, CInt] := Ok(10)
    result                := when x {
        ok:  Ok { ok.v }
        err: Err { return 1 }
    }
    if result != 10 { return 1 }
    0
}

// Test 6: diverging arm with break in a loop
test_when_break() {
    x: Option[CInt] := Some(5)
    i               := 0
    while i < 10 {
        val := when x {
            s: Some { s.v }
            n: None { break }
        }
        i += val
    }
    if i != 10 { return 1 }
    0
}

main() {
    fails := 0

    fails += test_when_return_void()
    fails += test_when_return_value()
    fails += test_if_else_diverge()
    fails += test_if_else_diverge_reverse()
    fails += test_when_multi_diverge()
    fails += test_when_break()

    if fails == 0 { c_printf("All diverging arm tests passed.\n") }
    else { c_printf("%d diverging arm test(s) failed.\n", fails) }

    fails
}
