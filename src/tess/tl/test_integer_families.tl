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

// CInt and Int (CLongLong) are both signed — they should unify and canonicalize.
test_signed_unification() {
    error := 0

    x: CInt := 42
    y: Int  := x
    error += if y == 42 {0} else {1}

    error
}

// --- 3. Unsigned family unification ---

// CSize and CUnsignedLongLong (UInt) are both unsigned — they should unify and canonicalize.
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

    // Some(42) creates Option[Int]
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

// --- 5. Same-family Option canonicalization ---

// Option[CInt] should produce the same monomorphization as Option[Int]
// because CInt canonicalizes to Int (CLongLong) during unification.
accept_option_int(o: Option[Int]) -> Int {
    when o {
        s: Some { s.v }
        n: None { -1 }
    }
}

test_option_canonicalization() {
    error := 0

    // Pass Option[CInt] where Option[Int] is expected — same signed family
    x: CInt := 77
    opt     := Some(x)
    error += if accept_option_int(opt) == 77 {0} else {1}

    error
}

// --- 6. sizeof returns CSize (unsigned), use with unsigned literals ---

test_sizeof_unsigned() {
    error := 0

    // sizeof returns CSize (unsigned) — compare with unsigned literals
    error += if sizeof[Int]() >= 8u {0} else {1}
    error += if sizeof[UInt]() >= 8u {0} else {1}

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
    error += test_option_canonicalization()
    error += test_sizeof_unsigned()

    error
}
