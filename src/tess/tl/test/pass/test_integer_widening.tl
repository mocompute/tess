#module main
#import <cstdint.tl>

// Implicit widening within a sub-chain (narrow -> wide) should succeed.

take_int (x: Int)  -> Int  { x }
take_uint(x: UInt) -> UInt { x }

// --- Widening within C-signed sub-chain ---

test_cshort_to_cint() {
    error     := 0
    a: CShort := 10
    b: CInt   := a                      // CShort -> CInt: widening OK
    error += if b == 10 {0} else {1}
    error
}

test_cint_to_int() {
    error   := 0
    a: CInt := 42
    b: Int  := a                        // CInt -> Int (CLongLong): widening OK
    error += if b == 42 {0} else {1}
    error
}

test_cshort_to_int() {
    error     := 0
    a: CShort := 7
    b: Int    := a                      // CShort -> Int: widening OK
    error += if b == 7 {0} else {1}
    error
}

// --- Widening within C-unsigned sub-chain ---

test_cuchar_to_uint() {
    error            := 0
    a: CUnsignedChar := 200u
    b: UInt          := a               // CUnsignedChar -> UInt: widening OK
    error += if b == 200u {0} else {1}
    error
}

test_cushort_to_cuint() {
    error             := 0
    a: CUnsignedShort := 1000u
    b: CUnsignedInt   := a              // CUnsignedShort -> CUnsignedInt: widening OK
    error += if b == 1000u {0} else {1}
    error
}

// --- Widening within fixed-width sub-chains ---

test_cint8_to_cint32() {
    error     := 0
    a: CInt8  := 42
    b: CInt32 := a                      // CInt8 -> CInt32: widening OK
    error += if b == 42 {0} else {1}
    error
}

test_cuint8_to_cuint32() {
    error      := 0
    a: CUInt8  := 200u
    b: CUInt32 := a                     // CUInt8 -> CUInt32: widening OK
    error += if b == 200u {0} else {1}
    error
}

// --- Widening through function call ---

test_funcall_widening() {
    error   := 0
    a: CInt := 99
    b       := take_int(a)              // CInt -> Int parameter: widening OK
    error += if b == 99 {0} else {1}
    error
}

// --- Widening through return type ---

return_cint_as_int() -> Int {
    a: CInt := 77
    a                                   // CInt -> Int return: widening OK
}

test_return_widening() {
    error := 0
    b     := return_cint_as_int()
    error += if b == 77 {0} else {1}
    error
}

// --- Fixed-width funcall widening ---

take_cint32(x: CInt32) -> CInt32 { x }

test_cint8_to_cint32_funcall() {
    error    := 0
    a: CInt8 := 42
    b        := take_cint32(a)          // CInt8 -> CInt32 parameter: widening OK
    error += if b == 42 {0} else {1}
    error
}

// --- Additional widening pairs (C-signed) ---

test_clong_to_int() {
    error    := 0
    a: CLong := 999
    b: Int   := a                       // CLong -> Int (CLongLong): widening OK
    error += if b == 999 {0} else {1}
    error
}

test_csignedchar_to_cshort() {
    error          := 0
    a: CSignedChar := 42
    b: CShort      := a                 // CSignedChar -> CShort: widening OK
    error += if b == 42 {0} else {1}
    error
}

// --- Additional widening pairs (C-unsigned) ---

test_cuint_to_culong() {
    error            := 0
    a: CUnsignedInt  := 5000u
    b: CUnsignedLong := a               // CUnsignedInt -> CUnsignedLong: widening OK
    error += if b == 5000u {0} else {1}
    error
}

// --- Additional widening pairs (fixed-width, skip) ---

test_cint16_to_cint64() {
    error     := 0
    a: CInt16 := 1000
    b: CInt64 := a                      // CInt16 -> CInt64: widening OK (skip)
    error += if b == 1000 {0} else {1}
    error
}

test_cuint16_to_cuint64() {
    error      := 0
    a: CUInt16 := 5000u
    b: CUInt64 := a                     // CUInt16 -> CUInt64: widening OK (skip)
    error += if b == 5000u {0} else {1}
    error
}

// --- Widening through function pointer call ---

take_int_fn(x: Int) -> Int { x }

test_funcptr_widening() {
    error   := 0
    f_ptr   := take_int_fn/1
    a: CInt := 42
    result  := f_ptr(a)                 // CInt widens to Int through function pointer
    error += if result == 42 {0} else {1}
    error
}

// --- Same-type assignment (trivially OK) ---

test_same_type() {
    error   := 0
    a: CInt := 55
    b: CInt := a                        // same type: always OK
    error += if b == 55 {0} else {1}
    error
}

main() {
    error := 0
    error += test_cshort_to_cint()
    error += test_cint_to_int()
    error += test_cshort_to_int()
    error += test_cuchar_to_uint()
    error += test_cushort_to_cuint()
    error += test_cint8_to_cint32()
    error += test_cuint8_to_cuint32()
    error += test_funcall_widening()
    error += test_return_widening()
    error += test_cint8_to_cint32_funcall()
    error += test_clong_to_int()
    error += test_csignedchar_to_cshort()
    error += test_cuint_to_culong()
    error += test_cint16_to_cint64()
    error += test_cuint16_to_cuint64()
    error += test_funcptr_widening()
    error += test_same_type()
    error
}
