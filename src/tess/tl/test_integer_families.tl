#module main
#import <stdint.tl>

// --- 1. Unsigned literal syntax ---

test_unsigned_literals() {
    error := 0

    // Basic unsigned literal
    a := 42u
    error += if a == 42u {0} else {1}

    // Uppercase suffix
    b := 100U
    error += if b == 100U {0} else {1}

    // Zero unsigned
    c := 0u
    error += if c == 0u {0} else {1}

    // Hex unsigned
    d := 0xFFu
    error += if d == 255u {0} else {1}

    // Binary unsigned
    e := 0b1010u
    error += if e == 10u {0} else {1}

    // Unsigned arithmetic
    f := 10u + 20u
    error += if f == 30u {0} else {1}

    error
}

// --- 2. Signed family unification ---

// CInt and Int (CLongLong) are both signed — they should unify.
test_signed_unification() {
    error := 0

    x: CInt := 42
    y: Int  := x
    error += if y == 42 {0} else {1}

    error
}

// --- 3. Unsigned family unification ---

// CSize and CUnsignedLongLong (UInt) are both unsigned — they should unify.
test_unsigned_unification() {
    error := 0

    x: CSize := 99u
    y: UInt  := x
    error += if y == 99u {0} else {1}

    error
}

// --- 4. Option with same-family types ---

test_option_signed_family() {
    error := 0

    // Some(42) creates Option[Int], should work
    opt := Some(42)
    error += when opt {
        s: Some { if s.v == 42 {0} else {1} }
        n: None { 1 }
    }

    error
}

test_option_unsigned_family() {
    error := 0

    // Some(0u) creates Option[UInt]
    opt := Some(0u)
    error += when opt {
        s: Some { if s.v == 0u {0} else {1} }
        n: None { 1 }
    }

    error
}

// --- 5. Cross-family compatibility ---

// Signed integer literal assigned to CSize (unsigned) should still work
test_cross_family() {
    error := 0

    sz: CSize := 42
    error += if sz == 42 {0} else {1}

    error
}

// --- 6. Bool is distinct from integers ---

test_bool_distinct() {
    error := 0

    // Bool values are not integers
    b := 1 == 1
    error += if b {0} else {1}

    error
}

// --- Main ---

main() {
    error := 0

    error += test_unsigned_literals()
    error += test_signed_unification()
    error += test_unsigned_unification()
    error += test_option_signed_family()
    error += test_option_unsigned_family()
    error += test_cross_family()
    error += test_bool_distinct()

    error
}
