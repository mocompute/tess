#module main

// Same-type operations: arithmetic, comparison, conditional with matching types.
// Also: weak literals resolve to concrete type from context.

test_same_type_arithmetic() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    c       := a + b                    // CInt + CInt: same type, OK
    error += if c == 30 {0} else {1}

    d: Int := 100
    e: Int := 200
    f      := d + e                     // Int + Int: same type, OK
    error += if f == 300 {0} else {1}

    error
}

test_same_type_comparison() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    error += if a < b {0} else {1}

    c: Int := 100
    d: Int := 200
    error += if c < d {0} else {1}

    error
}

test_same_type_conditional() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    c       := if 1 == 1 { a } else { b } // both CInt: OK
    error += if c == 10 {0} else {1}

    d: Int := 100
    e: Int := 200
    f      := if 1 == 1 { d } else { e } // both Int: OK
    error += if f == 100 {0} else {1}

    error
}

test_weak_literal_in_conditional() {
    error := 0

    // Weak literal 0 in else branch should resolve to CInt from a
    a: CInt := 10
    b       := if 1 == 1 { a } else { 0 } // 0 becomes CInt
    error += if b == 10 {0} else {1}

    // Weak literal 0 in else branch should resolve to Int from c
    c: Int := 100
    d      := if 1 == 1 { c } else { 0 } // 0 becomes Int
    error += if d == 100 {0} else {1}

    // Weak unsigned literal in else branch
    e: UInt := 42u
    f       := if 1 == 1 { e } else { 0u } // 0u becomes UInt
    error += if f == 42u {0} else {1}

    error
}

test_weak_literals_in_operators() {
    error := 0

    // Weak literal 5 resolves to CInt from context of a
    a: CInt := 10
    b       := a + 5                    // 5 becomes CInt, CInt + CInt OK
    error += if b == 15 {0} else {1}

    // Weak literal 50 resolves to Int from context of c
    c: Int := 100
    d      := c + 50                    // 50 becomes Int, Int + Int OK
    error += if d == 150 {0} else {1}

    // Weak unsigned literal
    e: UInt := 10u
    f       := e + 5u                   // 5u becomes UInt, UInt + UInt OK
    error += if f == 15u {0} else {1}

    error
}

main() {
    error := 0
    error += test_same_type_arithmetic()
    error += test_same_type_comparison()
    error += test_same_type_conditional()
    error += test_weak_literal_in_conditional()
    error += test_weak_literals_in_operators()
    error
}
