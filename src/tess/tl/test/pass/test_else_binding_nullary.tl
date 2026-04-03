// Test else binding when the else variant is nullary (no fields).
#module MyOpt

MyOption[T]: | MySome { value: T } | MyNone

#module main

// Test 1: else binding on nullary variant (MyNone has no fields)
test_nullary_match() -> CInt {
    val: MyOpt.MyOption[CInt] := MyOpt.MySome(42)
    s:   MySome               := val else n { return 1 }
    s.value - 42
}

// Test 2: else binding on nullary variant — none case
test_nullary_none() -> CInt {
    val: MyOpt.MyOption[CInt] := MyOpt.MyNone()
    s:   MySome               := val else n { return 0 }
    // Should not reach here
    1
}

// Test 3: primary variant is nullary, else gets the one with fields
test_primary_nullary() -> CInt {
    val: MyOpt.MyOption[CInt] := MyOpt.MySome(7)
    n:   MyNone               := val else s { return s.value - 7 }
    // Should not reach here (val is MySome, so else runs)
    1
}

main() {
    error: CInt := 0
    error += test_nullary_match()
    error += test_nullary_none()
    error += test_primary_nullary()
    error
}
