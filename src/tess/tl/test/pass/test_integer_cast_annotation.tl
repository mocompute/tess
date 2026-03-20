#module main
#import <cstdint.tl>

// Test that let-in type annotations work as explicit casts for integer types.
// This exercises the generalized is_cast_annotation() which recognizes integer
// annotations in addition to pointer annotations.

// --- Narrowing within C-signed sub-chain ---

test_int_to_cint() {
    error   := 0
    a: Int  := 42
    b: CInt := a                        // narrowing: Int -> CInt (explicit cast via annotation)
    error += if b == 42 {0} else {1}
    error
}

test_int_to_cshort() {
    error     := 0
    a: Int    := 7
    b: CShort := a                      // narrowing: Int -> CShort (explicit cast via annotation)
    error += if b == 7 {0} else {1}
    error
}

// --- Narrowing within C-unsigned sub-chain ---

test_uint_to_cuchar() {
    error            := 0
    a: UInt          := 200u
    b: CUnsignedChar := a               // narrowing: UInt -> CUnsignedChar
    error += if b == 200u {0} else {1}
    error
}

// --- Cross-family cast (signed <-> unsigned) ---

test_int_to_uint() {
    error   := 0
    a: Int  := 100
    b: UInt := a                        // cross-family: signed -> unsigned (explicit cast)
    error += if b == 100u {0} else {1}
    error
}

test_uint_to_int() {
    error   := 0
    a: UInt := 50u
    b: Int  := a                        // cross-family: unsigned -> signed (explicit cast)
    error += if b == 50 {0} else {1}
    error
}

// --- Cross-chain cast (C-signed -> fixed-signed) ---

test_cint_to_cint32() {
    error          := 0
    a:      CInt   := 42
    result: CInt32 := a                 // cross-chain: C-signed -> fixed-signed (explicit cast)
    error += if result == 42 {0} else {1}
    error
}

// --- Standalone type casts ---

test_uint_to_csize() {
    error           := 0
    a:        UInt  := 100u
    size:     CSize := a                // standalone: UInt -> CSize (explicit cast)
    expected: CSize := 100u
    error += if size == expected {0} else {1}
    error
}

test_int_to_cptrdiff() {
    error              := 0
    a:        Int      := 77
    diff:     CPtrDiff := a             // standalone: Int -> CPtrDiff (explicit cast)
    expected: CPtrDiff := 77
    error += if diff == expected {0} else {1}
    error
}

// --- Fixed-width narrowing casts ---

test_cint64_to_cint8() {
    error     := 0
    a: CInt64 := 42
    b: CInt8  := a                      // fixed-signed narrowing: CInt64 -> CInt8 (explicit cast)
    error += if b == 42 {0} else {1}
    error
}

test_cuint64_to_cuint8() {
    error      := 0
    a: CUInt64 := 200u
    b: CUInt8  := a                     // fixed-unsigned narrowing: CUInt64 -> CUInt8 (explicit cast)
    error += if b == 200u {0} else {1}
    error
}

test_cint32_to_cint16() {
    error     := 0
    a: CInt32 := 1000
    b: CInt16 := a                      // fixed-signed narrowing: CInt32 -> CInt16 (explicit cast)
    error += if b == 1000 {0} else {1}
    error
}

// --- CChar casts (standalone sub-chain) ---

test_int_to_cchar() {
    error    := 0
    a: Int   := 65
    b: CChar := a                       // cross-chain: Int -> CChar (explicit cast)
    error += if b == 'A' {0} else {1}
    error
}

test_cchar_to_int() {
    error    := 0
    a: CChar := 'A'
    b: Int   := a                       // cross-chain: CChar -> Int (explicit cast)
    error += if b == 65 {0} else {1}
    error
}

// --- Cast in return value ---

compute_int() -> Int { 99 }

test_return_cast() {
    error        := 0
    result: CInt := compute_int()       // narrowing via cast annotation
    error += if result == 99 {0} else {1}
    error
}

main() {
    error := 0
    error += test_int_to_cint()
    error += test_int_to_cshort()
    error += test_uint_to_cuchar()
    error += test_int_to_uint()
    error += test_uint_to_int()
    error += test_cint_to_cint32()
    error += test_uint_to_csize()
    error += test_int_to_cptrdiff()
    error += test_cint64_to_cint8()
    error += test_cuint64_to_cuint8()
    error += test_cint32_to_cint16()
    error += test_int_to_cchar()
    error += test_cchar_to_int()
    error += test_return_cast()
    result: CInt := error
    result
}
