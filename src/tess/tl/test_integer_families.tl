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

    x: CSize := 99zu
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

// --- 5. Same-family type assignment ---

// CInt value can be assigned to Int variable (same signed family).
test_signed_family_assignment() {
    error := 0

    x: CInt := 77
    y: Int  := x
    error += if y == 77 {0} else {1}

    error
}

// --- 6. sizeof returns CSize (unsigned), use with unsigned literals ---

test_sizeof_unsigned() {
    error := 0

    // sizeof returns CSize (unsigned) — compare with unsigned literals
    error += if sizeof[Int]() >= 8zu {0} else {1}
    error += if sizeof[UInt]() >= 8zu {0} else {1}

    error
}

// --- 7. Narrow types preserve their C ABI type ---

// Narrow types (CInt8, CUInt8, etc.) unify within their family but are not
// canonicalized, preserving their specific C type in codegen.
test_narrow_signed() {
    error := 0

    x: CInt8 := 42
    y: Int   := x    // narrow signed -> canonical signed: same family, OK
    error += if y == 42 {0} else {1}

    // CInt8 preserves its size (1 byte) despite unifying with Int
    error += if sizeof[CInt8]() == 1zu {0} else {1}

    a: CInt16 := 1000
    b: Int    := a
    error += if b == 1000 {0} else {1}
    error += if sizeof[CInt16]() == 2zu {0} else {1}

    error
}

test_narrow_unsigned() {
    error := 0

    x: CUInt8 := 200u
    y: UInt   := x    // narrow unsigned -> canonical unsigned: same family, OK
    error += if y == 200u {0} else {1}

    error += if sizeof[CUInt8]() == 1zu {0} else {1}

    a: CUInt16 := 50000u
    b: UInt    := a
    error += if b == 50000u {0} else {1}
    error += if sizeof[CUInt16]() == 2zu {0} else {1}

    error
}

// --- 8. Non-canonical family members ---

// CLong, CShort, etc. are signed-family members distinct from the canonical CLongLong.
// CUnsignedLong, CUnsignedShort are unsigned-family members distinct from CUnsignedLongLong.
test_non_canonical_signed() {
    error := 0

    a: CShort := 123
    b: Int    := a
    error += if b == 123 {0} else {1}

    c: CLong := 456
    d: Int   := c
    error += if d == 456 {0} else {1}

    // CPtrDiff is signed
    e: CPtrDiff := -10z
    f: Int      := e
    error += if f == -10 {0} else {1}

    error
}

test_non_canonical_unsigned() {
    error := 0

    a: CUnsignedShort := 123u
    b: UInt           := a
    error += if b == 123u {0} else {1}

    c: CUnsignedLong := 456u
    d: UInt          := c
    error += if d == 456u {0} else {1}

    e: CUnsignedInt := 789u
    f: UInt         := e
    error += if f == 789u {0} else {1}

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
    error += test_signed_family_assignment()
    error += test_sizeof_unsigned()
    error += test_narrow_signed()
    error += test_narrow_unsigned()
    error += test_non_canonical_signed()
    error += test_non_canonical_unsigned()

    error
}
