#module MyOption

T[a]: | MySome { v: a }
      | MyNone

#module main

// Test 1: Basic bail with return
test_basic_bail() {
    val := MyOption.MySome(42)
    s: MySome := val else { return 1 }
    if s.v == 42 {0} else {1}
}

// Test 2: Bail takes the else path
test_bail_else_path() {
    val := if 0 == 1 { MyOption.MySome(0) } else { MyOption.MyNone() }
    s: MySome := val else { return 0 }
    // Should not reach here
    1
}

// Test 3: Bail with non-trivial remaining body
test_bail_remaining_body() {
    val := MyOption.MySome(10)
    s: MySome := val else { return 1 }
    x := s.v + 5
    y := x * 2
    if y == 30 {0} else {1}
}

// Test 4: Multiple sequential bails
test_multiple_bails() {
    a := MyOption.MySome(10)
    b := MyOption.MySome(20)
    x: MySome := a else { return 1 }
    y: MySome := b else { return 1 }
    if x.v + y.v == 30 {0} else {1}
}

// Test 5: Bail with generic tagged union (Float)
test_bail_generic() {
    val := MyOption.MySome(3.14)
    s: MySome := val else { return 1 }
    if s.v > 3.0 {0} else {1}
}

// Test 6: Bail inside a while loop with break
test_bail_break() {
    val   := MyOption.MySome(99)
    result := 1
    while 1 {
        s: MySome := val else { break }
        result = if s.v == 99 {0} else {1}
        break
    }
    result
}

// Test 7: Bail inside a while loop with continue
test_bail_continue() {
    result := 0
    i      := 0
    while i < 3 {
        v := if i == 1 { MyOption.MySome(42) } else { MyOption.MyNone() }
        i = i + 1
        s: MySome := v else { continue }
        result = result + s.v
    }
    if result == 42 {0} else {1}
}

main() {
    error := 0
    error += test_basic_bail()
    error += test_bail_else_path()
    error += test_bail_remaining_body()
    error += test_multiple_bails()
    error += test_bail_generic()
    error += test_bail_break()
    error += test_bail_continue()
    error
}
