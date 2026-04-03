#module Shape

Shape: | Circle { r: CInt }
       | Square { s: CInt }

#module MyOption

T[a]: | MySome { value: a }
      | MyNone

#module main

// ---------------------------------------------------------------------------
// test_in_basic: a binding at the end of a block returns the bound value.
// ---------------------------------------------------------------------------

test_in_basic() {
    a := 99                             // declares a
    b := 1                              // declares b
    a = 1                               // reassigns to a
    a - b                               // returns result
}

// ---------------------------------------------------------------------------
// test_in_expression: let-in as an expression.
// ---------------------------------------------------------------------------

test_in_expression() {
    res := (x := 42                     // line break here is optional
            x - 42)
    res
}

// ---------------------------------------------------------------------------
// test_in_trailing: a binding at the end of a block (no body) evaluates to
// the bound value.
// ---------------------------------------------------------------------------

_trailing_foo(value: CInt) {
    result: CInt := value
}

_trailing_bar() {
    a      := 10
    b      := 20
    result := a + b
}

test_in_trailing() {
    _trailing_foo(42) - 42 + _trailing_bar() - 30
}

// ---------------------------------------------------------------------------
// test_else_binding_in_else: a := binding inside a let-else's else block.
// ---------------------------------------------------------------------------

test_else_binding_in_else() {
    opt: Option[Int] := Some(42)
    i                := 0
    while i < 1 {
        s: Some := opt else {
            x := 99                     // <-- this := causes the parse error
            i += 1
            continue
        }
        i += 1
    }
    ret: CInt := 0
    ret
}

// ---------------------------------------------------------------------------
// test_else_complex_else: let-else where else block has if/else before
// diverging return.
// ---------------------------------------------------------------------------

test_else_complex_else() {
    s: Shape.Shape := Shape.Square(s = 4)
    c: Circle      := s else {
        fallback := 1
        if fallback == 1 {
            return 0
        } else {
            return 1
        }
    }
    c.r
}

// ---------------------------------------------------------------------------
// test_else_non_diverging: let-else with non-diverging fallback values.
// ---------------------------------------------------------------------------

_nd_test_match_path() {
    val       := MyOption.MySome(42)
    s: MySome := val else { return 1 }
    if s.value != 42 { return 1 }
    0
}

_nd_no_match_helper() {
    val: MyOption.T[CInt] := MyOption.MyNone
    s:   MySome           := val else { 99 }
    s.value + 1
}

_nd_test_no_match_fallback() {
    if _nd_no_match_helper() != 99 { return 1 }
    0
}

_nd_match_helper() {
    val       := MyOption.MySome(10)
    s: MySome := val else { 99 }
    s.value + 1
}

_nd_test_match_continuation() {
    if _nd_match_helper() != 11 { return 1 }
    0
}

_nd_test_diverging_still_works() {
    val: MyOption.T[CInt] := MyOption.MyNone
    s:   MySome           := val else { return 0 }
    1
}

test_else_non_diverging() {
    r := _nd_test_match_path()
    r = r + _nd_test_no_match_fallback()
    r = r + _nd_test_match_continuation()
    r = r + _nd_test_diverging_still_works()
    r
}

// ---------------------------------------------------------------------------
// main: run all tests and sum results.
// ---------------------------------------------------------------------------

main() {
    r := test_in_basic()
    r = r + test_in_expression()
    r = r + test_in_trailing()
    r = r + test_else_binding_in_else()
    r = r + test_else_complex_else()
    r = r + test_else_non_diverging()
    r
}
