#module main
#import <stdint.tl>

// Test: weak signed literal resolves from function parameter context
take_cint  (x: CInt)         { x }
take_cint8 (x: CInt8)        { x }
take_cshort(x: CShort)       { x }
take_cuint (x: CUnsignedInt) { x }
take_cuint8(x: CUInt8)       { x }

test_signed_context() {
    error := 0
    error += if take_cint(42) == 42 {0} else {1}
    error += if take_cint8(5) == 5 {0} else {1}
    error += if take_cshort(100) == 100 {0} else {1}
    error
}

test_unsigned_context() {
    error := 0
    error += if take_cuint(42u) == 42u {0} else {1}
    error += if take_cuint8(5u) == 5u {0} else {1}
    error
}

// Test: weak literal resolves to additional fixed-width and C-named types
take_cint16 (x: CInt16)         { x }
take_cint32 (x: CInt32)         { x }
take_cint64 (x: CInt64)         { x }
take_clong  (x: CLong)          { x }
take_cuint16(x: CUInt16)        { x }
take_cuint32(x: CUInt32)        { x }
take_cuint64(x: CUInt64)        { x }
take_cushort(x: CUnsignedShort) { x }

test_signed_fixed_width_context() {
    error := 0
    error += if take_cint16(42) == 42 {0} else {1}
    error += if take_cint32(42) == 42 {0} else {1}
    error += if take_cint64(42) == 42 {0} else {1}
    error += if take_clong(42) == 42 {0} else {1}
    error
}

test_unsigned_fixed_width_context() {
    error := 0
    error += if take_cuint16(42u) == 42u {0} else {1}
    error += if take_cuint32(42u) == 42u {0} else {1}
    error += if take_cuint64(42u) == 42u {0} else {1}
    error += if take_cushort(42u) == 42u {0} else {1}
    error
}

// Test: two weak literals merging in a generic call
first[T](x: T, y: T) -> T { x }

test_weak_literal_merging() {
    error := 0
    // Both 42 and 99 are weak signed — they should merge and default to Int
    result := first(42, 99)
    error += if result == 42 {0} else {1}

    // Both 10u and 20u are weak unsigned — they should merge and default to UInt
    result2 := first(10u, 20u)
    error += if result2 == 10u {0} else {1}
    error
}

test_default_to_int() {
    error := 0
    x     := 42                         // should default to Int
    y     := 42u                        // should default to UInt
    error
}

test_z_literals() {
    error := 0
    x     := 42z                        // CPtrDiff
    y     := 42zu                       // CSize
    error += if x == 42z {0} else {1}
    error += if y == 42zu {0} else {1}
    error
}

main() {
    error := 0
    error += test_signed_context()
    error += test_unsigned_context()
    error += test_signed_fixed_width_context()
    error += test_unsigned_fixed_width_context()
    error += test_weak_literal_merging()
    error += test_default_to_int()
    error += test_z_literals()
    error
}
