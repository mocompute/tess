#module main
#import <cstdint.tl>

// Test compound assignment operators with integer types.
// Weak literals in compound assignment should resolve to the variable's type.

test_cint_compound() {
    error := 0

    a: CInt := 10
    a += 5                              // 5 is weak literal -> resolves to CInt
    error += if a == 15 {0} else {1}

    a -= 3
    error += if a == 12 {0} else {1}

    a *= 2
    error += if a == 24 {0} else {1}

    error
}

test_int_compound() {
    error := 0

    a: Int := 100
    a += 50
    error += if a == 150 {0} else {1}

    a -= 25
    error += if a == 125 {0} else {1}

    a *= 3
    error += if a == 375 {0} else {1}

    error
}

test_uint_compound() {
    error := 0

    a: UInt := 100u
    a += 50u
    error += if a == 150u {0} else {1}

    a -= 25u
    error += if a == 125u {0} else {1}

    a *= 2u
    error += if a == 250u {0} else {1}

    error
}

test_cint8_compound() {
    error := 0

    a: CInt8 := 10
    a += 5
    error += if a == 15 {0} else {1}

    a -= 3
    error += if a == 12 {0} else {1}

    error
}

main() {
    error := 0
    error += test_cint_compound()
    error += test_int_compound()
    error += test_uint_compound()
    error += test_cint8_compound()
    error
}
