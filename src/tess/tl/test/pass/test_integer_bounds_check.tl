#module main
#import <stdint.tl>

// Test that integer narrowing casts produce correct results when the
// value fits in the target type. With --bounds-check enabled, the
// bounds-check macro should NOT fire for these (values are in range).

test_narrowing_in_range() {
    error := 0
    a: Int := 42
    b: CInt := a
    error += if b == 42 {0} else {1}
    error
}

test_unsigned_narrowing_in_range() {
    error := 0
    a: UInt := 200u
    b: CUnsignedChar := a
    error += if b == 200u {0} else {1}
    error
}

test_cross_family_in_range() {
    error := 0
    a: Int := 100
    b: UInt := a
    error += if b == 100u {0} else {1}
    c: UInt := 50u
    d: Int := c
    error += if d == 50 {0} else {1}
    error
}

main() {
    error := 0
    error += test_narrowing_in_range()
    error += test_unsigned_narrowing_in_range()
    error += test_cross_family_in_range()
    result: CInt := error
    result
}
