#module main

// Implicit widening should work for reassignment, not just let-in bindings.

test_cshort_to_int() {
    x: CShort := 1
    y: Int    := 0
    y = x                               // CShort -> Int: widening reassignment should succeed
    if y == 1 {0} else {1}
}

test_cint_to_int() {
    x: CInt := 42
    y: Int  := 0
    y = x                               // CInt -> Int: widening reassignment should succeed
    if y == 42 {0} else {1}
}

test_cuchar_to_uint() {
    x: CUnsignedChar := 200u
    y: UInt          := 0u
    y = x                               // CUnsignedChar -> UInt: widening reassignment should succeed
    if y == 200u {0} else {1}
}

test_cint8_to_cint32() {
    x: CInt8  := 42
    y: CInt32 := 0
    y = x                               // CInt8 -> CInt32: widening reassignment should succeed
    if y == 42 {0} else {1}
}

main() {
    error := 0
    error += test_cshort_to_int()
    error += test_cint_to_int()
    error += test_cuchar_to_uint()
    error += test_cint8_to_cint32()
    error
}
