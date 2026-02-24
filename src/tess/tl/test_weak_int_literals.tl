#module main
#import <stdint.tl>

// Test: weak signed literal resolves from function parameter context
take_cint(x: CInt) { x }
take_cint8(x: CInt8) { x }
take_cshort(x: CShort) { x }
take_cuint(x: CUnsignedInt) { x }
take_cuint8(x: CUInt8) { x }

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

test_default_to_int() {
    error := 0
    x := 42        // should default to Int
    y := 42u       // should default to UInt
    error
}

test_z_literals() {
    error := 0
    x := 42z       // CPtrDiff
    y := 42zu      // CSize
    error += if x == 42z {0} else {1}
    error += if y == 42zu {0} else {1}
    error
}

main() {
    error := 0
    error += test_signed_context()
    error += test_unsigned_context()
    error += test_default_to_int()
    error += test_z_literals()
    error
}
