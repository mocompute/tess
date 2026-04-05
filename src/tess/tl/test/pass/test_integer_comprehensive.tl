// Combined integer test suite.
// Original files:
//   test_integer_types.tl, test_integer_types_cast.tl,
//   test_integer_cast_annotation.tl, test_integer_families.tl,
//   test_integer_widening.tl, test_integer_widening_reassign.tl,
//   test_integer_weak_literals.tl, test_integer_weak_module_const.tl,
//   test_integer_weak_to_standalone.tl, test_integer_weak_unsigned_update.tl,
//   test_integer_bounds_check.tl, test_integer_compound_assign.tl,
//   test_integer_generic_type_args.tl, test_integer_narrowing_let_cast.tl,
//   test_integer_same_type.tl, test_integer_standalone_cchar.tl,
//   test_integer_toint.tl, test_integer_z_literals.tl

#module main
#import <cstdio.tl>
#import <cstdint.tl>
#import <Unsafe.tl>
#import <String.tl>
#import <ToInt.tl>
#import <Slice.tl>

// ============================================================================
// Module-level constant from test_integer_weak_module_const.tl
// ============================================================================

_NUL: Byte := 0u

// ============================================================================
// Helper functions
// ============================================================================

// --- from test_integer_widening.tl ---
take_int   (x: Int)    -> Int    { x }
take_uint  (x: UInt)   -> UInt   { x }
take_cint32(x: CInt32) -> CInt32 { x }
take_int_fn(x: Int)    -> Int    { x }

return_cint_as_int() -> Int {
    a: CInt := 77
    a
}

// --- from test_integer_cast_annotation.tl ---
compute_int() -> Int { 99 }

// --- from test_integer_weak_literals.tl ---
take_cint  (x: CInt)         { x }
take_cint8 (x: CInt8)        { x }
take_cshort(x: CShort)       { x }
take_cuint (x: CUnsignedInt) { x }
take_cuint8(x: CUInt8)       { x }

take_cint16      (x: CInt16)         { x }
take_cint32_weak (x: CInt32)         { x }
take_cint64      (x: CInt64)         { x }
take_clong       (x: CLong)          { x }
take_cuint16     (x: CUInt16)        { x }
take_cuint32     (x: CUInt32)        { x }
take_cuint64     (x: CUInt64)        { x }
take_cushort     (x: CUnsignedShort) { x }

first[T](x: T, y: T) -> T { x }

// --- from test_integer_weak_to_standalone.tl ---
take_size   (x: CSize)    -> CSize    { x }
take_ptrdiff(x: CPtrDiff) -> CPtrDiff { x }

// --- from test_integer_standalone_cchar.tl ---
take_cchar(x: CChar) -> CChar { x }

// --- from test_integer_toint.tl ---
sum_while(args: ...ToInt) -> Int {
    total: Int   := 0
    i:     CSize := 0
    while i < args.size, i += 1 {
        total += args.data.[i]
    }
    total
}

sum_for(args: ...ToInt) -> Int {
    total: Int := 0
    for x in Slice args {
        total += x
    }
    total
}

// --- from test_integer_generic_type_args.tl ---
wrap[T]         (x: T)       -> T { x }
pair_first[T, U](x: T, y: U) -> T { x }

// ============================================================================
// From test_integer_types.tl
// ============================================================================

test_types() {
    error := 0

    error += if 1zu == sizeof[CChar]() {0} else {1}
    error += if 1zu == sizeof[CUnsignedChar]() {0} else {1}
    error += if 1zu == sizeof[CSignedChar]() {0} else {1}

    error += if sizeof[CShort]() == sizeof[CUnsignedShort]() {0} else {1}
    error += if sizeof[CInt]() == sizeof[CUnsignedInt]() {0} else {1}
    error += if sizeof[CLong]() == sizeof[CUnsignedLong]() {0} else {1}

    error += if sizeof[CShort]() >= 2zu {0} else {1}
    error += if sizeof[CInt]() >= 2zu {0} else {1}
    error += if sizeof[CLong]() >= 4zu {0} else {1}
    error += if sizeof[CLongLong]() >= 8zu {0} else {1}

    error += if 8zu == sizeof[CSize]() {0} else {1}
    error += if 8zu == sizeof[CPtrDiff]() {0} else {1}

    // fixed width integers
    error += if 1zu == sizeof[CInt8]() {0} else {1}
    error += if 1zu == sizeof[CUInt8]() {0} else {1}
    error += if 2zu == sizeof[CInt16]() {0} else {1}
    error += if 2zu == sizeof[CUInt16]() {0} else {1}
    error += if 4zu == sizeof[CInt32]() {0} else {1}
    error += if 4zu == sizeof[CUInt32]() {0} else {1}
    error += if 8zu == sizeof[CInt64]() {0} else {1}
    error += if 8zu == sizeof[CUInt64]() {0} else {1}

    error
}

// ============================================================================
// From test_integer_types_cast.tl
// ============================================================================

test_types_cast() {
    error := 0

    sz: CSize := c_SIZE_MAX
    error += if 8zu == sizeof(sz) {0} else {1}

    ui8: CUInt8 := sz
    error += if 1zu == sizeof(ui8) {0} else {1}
    error += if 0377u == ui8 {0} else {1} // 0 prefix: octal, like C

    i8: CInt8 := sz
    error += if 1zu == sizeof(i8) {0} else {1}
    error += if i8 == -1 {0} else {1}

    ui16: CUInt16 := sz
    error += if 2zu == sizeof(ui16) {0} else {1}
    error += if 0xffffu == ui16 {0} else {1} // 0x prefix: hex, like C

    i16: CInt16 := sz
    error += if 2zu == sizeof(i16) {0} else {1}
    error += if - 1 == i16 {0} else {1}

    error
}

// ============================================================================
// From test_integer_cast_annotation.tl
// ============================================================================

test_int_to_cint() {
    error   := 0
    a: Int  := 42
    b: CInt := a
    error += if b == 42 {0} else {1}
    error
}

test_int_to_cshort() {
    error     := 0
    a: Int    := 7
    b: CShort := a
    error += if b == 7 {0} else {1}
    error
}

test_uint_to_cuchar() {
    error            := 0
    a: UInt          := 200u
    b: CUnsignedChar := a
    error += if b == 200u {0} else {1}
    error
}

test_int_to_uint() {
    error   := 0
    a: Int  := 100
    b: UInt := a
    error += if b == 100u {0} else {1}
    error
}

test_uint_to_int() {
    error   := 0
    a: UInt := 50u
    b: Int  := a
    error += if b == 50 {0} else {1}
    error
}

test_cint_to_cint32() {
    error          := 0
    a:      CInt   := 42
    result: CInt32 := a
    error += if result == 42 {0} else {1}
    error
}

test_uint_to_csize() {
    error           := 0
    a:        UInt  := 100u
    size:     CSize := a
    expected: CSize := 100u
    error += if size == expected {0} else {1}
    error
}

test_int_to_cptrdiff() {
    error              := 0
    a:        Int      := 77
    diff:     CPtrDiff := a
    expected: CPtrDiff := 77
    error += if diff == expected {0} else {1}
    error
}

test_cint64_to_cint8() {
    error     := 0
    a: CInt64 := 42
    b: CInt8  := a
    error += if b == 42 {0} else {1}
    error
}

test_cuint64_to_cuint8() {
    error      := 0
    a: CUInt64 := 200u
    b: CUInt8  := a
    error += if b == 200u {0} else {1}
    error
}

test_cint32_to_cint16() {
    error     := 0
    a: CInt32 := 1000
    b: CInt16 := a
    error += if b == 1000 {0} else {1}
    error
}

test_int_to_cchar() {
    error    := 0
    a: Int   := 65
    b: CChar := a
    error += if b == 'A' {0} else {1}
    error
}

test_cchar_to_int() {
    error    := 0
    a: CChar := 'A'
    b: Int   := a
    error += if b == 65 {0} else {1}
    error
}

test_return_cast() {
    error        := 0
    result: CInt := compute_int()
    error += if result == 99 {0} else {1}
    error
}

test_cast_annotation() {
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
    error
}

// ============================================================================
// From test_integer_families.tl
// ============================================================================

test_unsigned_literals() {
    error := 0

    a := 42u
    error += if a == 42u {0} else {1}

    b := 100U
    error += if b == 100U {0} else {1}

    c := 0u
    error += if c == 0u {0} else {1}

    d := 0xFFu
    error += if d == 255u {0} else {1}

    e := 0b1010u
    error += if e == 10u {0} else {1}

    f := 10u + 20u
    error += if f == 30u {0} else {1}

    error
}

test_signed_unification() {
    error := 0

    x: CInt := 42
    y: Int  := x
    error += if y == 42 {0} else {1}

    error
}

test_option_signed_family() {
    error := 0

    opt := Some(42)
    error += when opt {
        s: Some { if s.value == 42 {0} else {1} }
        n: None { 1 }
    }

    error
}

test_option_unsigned_family() {
    error := 0

    opt := Some(0u)
    error += when opt {
        s: Some { if s.value == 0u {0} else {1} }
        n: None { 1 }
    }

    error
}

test_signed_family_assignment() {
    error := 0

    x: CInt := 77
    y: Int  := x
    error += if y == 77 {0} else {1}

    error
}

test_sizeof_unsigned() {
    error := 0

    error += if sizeof[Int]() >= 8zu {0} else {1}
    error += if sizeof[UInt]() >= 8zu {0} else {1}

    error
}

test_narrow_signed() {
    error := 0

    x: CInt8 := 42
    y: Int   := x
    error += if y == 42 {0} else {1}

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
    y: UInt   := x
    error += if y == 200u {0} else {1}

    error += if sizeof[CUInt8]() == 1zu {0} else {1}

    a: CUInt16 := 50000u
    b: UInt    := a
    error += if b == 50000u {0} else {1}
    error += if sizeof[CUInt16]() == 2zu {0} else {1}

    error
}

test_non_canonical_signed() {
    error := 0

    a: CShort := 123
    b: Int    := a
    error += if b == 123 {0} else {1}

    c: CLong := 456
    d: Int   := c
    error += if d == 456 {0} else {1}

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

test_families() {
    error := 0

    error += test_unsigned_literals()
    error += test_signed_unification()
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

// ============================================================================
// From test_integer_widening.tl
// ============================================================================

test_cshort_to_cint() {
    error     := 0
    a: CShort := 10
    b: CInt   := a
    error += if b == 10 {0} else {1}
    error
}

test_cint_to_int() {
    error   := 0
    a: CInt := 42
    b: Int  := a
    error += if b == 42 {0} else {1}
    error
}

test_cshort_to_int() {
    error     := 0
    a: CShort := 7
    b: Int    := a
    error += if b == 7 {0} else {1}
    error
}

test_cuchar_to_uint() {
    error            := 0
    a: CUnsignedChar := 200u
    b: UInt          := a
    error += if b == 200u {0} else {1}
    error
}

test_cushort_to_cuint() {
    error             := 0
    a: CUnsignedShort := 1000u
    b: CUnsignedInt   := a
    error += if b == 1000u {0} else {1}
    error
}

test_cint8_to_cint32() {
    error     := 0
    a: CInt8  := 42
    b: CInt32 := a
    error += if b == 42 {0} else {1}
    error
}

test_cuint8_to_cuint32() {
    error      := 0
    a: CUInt8  := 200u
    b: CUInt32 := a
    error += if b == 200u {0} else {1}
    error
}

test_funcall_widening() {
    error   := 0
    a: CInt := 99
    b       := take_int(a)
    error += if b == 99 {0} else {1}
    error
}

test_return_widening() {
    error := 0
    b     := return_cint_as_int()
    error += if b == 77 {0} else {1}
    error
}

test_cint8_to_cint32_funcall() {
    error    := 0
    a: CInt8 := 42
    b        := take_cint32(a)
    error += if b == 42 {0} else {1}
    error
}

test_clong_to_int() {
    error    := 0
    a: CLong := 999
    b: Int   := a
    error += if b == 999 {0} else {1}
    error
}

test_csignedchar_to_cshort() {
    error          := 0
    a: CSignedChar := 42
    b: CShort      := a
    error += if b == 42 {0} else {1}
    error
}

test_cuint_to_culong() {
    error            := 0
    a: CUnsignedInt  := 5000u
    b: CUnsignedLong := a
    error += if b == 5000u {0} else {1}
    error
}

test_cint16_to_cint64() {
    error     := 0
    a: CInt16 := 1000
    b: CInt64 := a
    error += if b == 1000 {0} else {1}
    error
}

test_cuint16_to_cuint64() {
    error      := 0
    a: CUInt16 := 5000u
    b: CUInt64 := a
    error += if b == 5000u {0} else {1}
    error
}

test_funcptr_widening() {
    error   := 0
    f_ptr   := take_int_fn/1
    a: CInt := 42
    result  := f_ptr(a)
    error += if result == 42 {0} else {1}
    error
}

test_same_type() {
    error   := 0
    a: CInt := 55
    b: CInt := a
    error += if b == 55 {0} else {1}
    error
}

test_widening() {
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

// ============================================================================
// From test_integer_widening_reassign.tl
// (Renamed to avoid collisions with test_integer_widening.tl)
// ============================================================================

test_cshort_to_int_reassign() {
    x: CShort := 1
    y: Int    := 0
    y = x
    if y == 1 {0} else {1}
}

test_cint_to_int_reassign() {
    x: CInt := 42
    y: Int  := 0
    y = x
    if y == 42 {0} else {1}
}

test_cuchar_to_uint_reassign() {
    x: CUnsignedChar := 200u
    y: UInt          := 0u
    y = x
    if y == 200u {0} else {1}
}

test_cint8_to_cint32_reassign() {
    x: CInt8  := 42
    y: CInt32 := 0
    y = x
    if y == 42 {0} else {1}
}

test_widening_reassign() {
    error := 0
    error += test_cshort_to_int_reassign()
    error += test_cint_to_int_reassign()
    error += test_cuchar_to_uint_reassign()
    error += test_cint8_to_cint32_reassign()
    error
}

// ============================================================================
// From test_integer_weak_literals.tl
// (take_cint32 renamed to take_cint32_weak to avoid collision)
// ============================================================================

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

test_signed_fixed_width_context() {
    error := 0
    error += if take_cint16(42) == 42 {0} else {1}
    error += if take_cint32_weak(42) == 42 {0} else {1}
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

test_weak_literal_merging() {
    error  := 0
    result := first(42, 99)
    error += if result == 42 {0} else {1}

    result2 := first(10u, 20u)
    error += if result2 == 10u {0} else {1}
    error
}

test_default_to_int() {
    error := 0
    x     := 42
    y     := 42u
    error
}

test_z_literals() {
    error := 0
    x     := 42z
    y     := 42zu
    error += if x == 42z {0} else {1}
    error += if y == 42zu {0} else {1}
    error
}

test_weak_literals() {
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

// ============================================================================
// From test_integer_weak_module_const.tl
// ============================================================================

test_weak_module_const() {
    s := String.from_cstr(c"abc")
    b := (r: Some := String.byte_at(s, 0zu) else { _NUL }, r.value)
    if b == 97u { 0 } else { 1 }
}

// ============================================================================
// From test_integer_weak_to_standalone.tl
// ============================================================================

test_weak_to_standalone() {
    error := 0
    error += if take_size(0) == 0zu { 0 } else { 1 }
    error += if take_size(42) == 42zu { 0 } else { 1 }
    error += if take_ptrdiff(42) == 42z { 0 } else { 1 }
    error
}

// ============================================================================
// From test_integer_weak_unsigned_update.tl
// ============================================================================

test_weak_unsigned_update() {
    error := 0

    // While update: UInt += unsuffixed literal
    i: UInt := 0u
    while i < 10u, i += 1 {
    }
    error += if i == 10u { 0 } else { 1 }

    // While update: CSize += unsuffixed literal
    k: CSize := 0zu
    while k < 10zu, k += 1 {
    }
    error += if k == 10zu { 0 } else { 1 }

    // Assignment add: UInt += unsuffixed literal
    j: UInt := 5u
    j += 3
    error += if j == 8u { 0 } else { 1 }

    error
}

// ============================================================================
// From test_integer_bounds_check.tl
// ============================================================================

test_narrowing_in_range() {
    error   := 0
    a: Int  := 42
    b: CInt := a
    error += if b == 42 {0} else {1}
    error
}

test_unsigned_narrowing_in_range() {
    error            := 0
    a: UInt          := 200u
    b: CUnsignedChar := a
    error += if b == 200u {0} else {1}
    error
}

test_cross_family_in_range() {
    error   := 0
    a: Int  := 100
    b: UInt := a
    error += if b == 100u {0} else {1}
    c: UInt := 50u
    d: Int  := c
    error += if d == 50 {0} else {1}
    error
}

test_bounds_check() {
    error := 0
    error += test_narrowing_in_range()
    error += test_unsigned_narrowing_in_range()
    error += test_cross_family_in_range()
    error
}

// ============================================================================
// From test_integer_compound_assign.tl
// ============================================================================

test_cint_compound() {
    error := 0

    a: CInt := 10
    a += 5
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

test_compound_assign() {
    error := 0
    error += test_cint_compound()
    error += test_int_compound()
    error += test_uint_compound()
    error += test_cint8_compound()
    error
}

// ============================================================================
// From test_integer_generic_type_args.tl
// ============================================================================

test_distinct_specializations() {
    error := 0

    a: CInt := 42
    b: Int  := 99
    ra      := wrap(a)
    rb      := wrap(b)
    error += if ra == 42 {0} else {1}
    error += if rb == 99 {0} else {1}

    c: CInt8  := 10
    d: CInt32 := 1000
    rc        := wrap(c)
    rd        := wrap(d)
    error += if rc == 10 {0} else {1}
    error += if rd == 1000 {0} else {1}

    e: UInt   := 42u
    f: CUInt8 := 200u
    re        := wrap(e)
    rf        := wrap(f)
    error += if re == 42u {0} else {1}
    error += if rf == 200u {0} else {1}

    error
}

test_generic_pair() {
    error := 0

    a: CInt := 42
    b: Int  := 99
    result  := pair_first(a, b)
    error += if result == 42 {0} else {1}

    c: CInt8 := 10
    d: UInt  := 50u
    result2  := pair_first(c, d)
    error += if result2 == 10 {0} else {1}

    error
}

test_generic_type_args() {
    error := 0
    error += test_distinct_specializations()
    error += test_generic_pair()
    error
}

// ============================================================================
// From test_integer_narrowing_let_cast.tl
// ============================================================================

test_narrowing_let_cast() {
    a: Int  := 42
    b       := a
    c: CInt := b
    error   := 0
    error += if c == 42 {0} else {1}
    error
}

// ============================================================================
// From test_integer_same_type.tl
// ============================================================================

test_same_type_arithmetic() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    c       := a + b
    error += if c == 30 {0} else {1}

    d: Int := 100
    e: Int := 200
    f      := d + e
    error += if f == 300 {0} else {1}

    error
}

test_same_type_comparison() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    error += if a < b {0} else {1}

    c: Int := 100
    d: Int := 200
    error += if c < d {0} else {1}

    error
}

test_same_type_conditional() {
    error := 0

    a: CInt := 10
    b: CInt := 20
    c       := if 1 == 1 { a } else { b }
    error += if c == 10 {0} else {1}

    d: Int := 100
    e: Int := 200
    f      := if 1 == 1 { d } else { e }
    error += if f == 100 {0} else {1}

    error
}

test_weak_literal_in_conditional() {
    error := 0

    a: CInt := 10
    b       := if 1 == 1 { a } else { 0 }
    error += if b == 10 {0} else {1}

    c: Int := 100
    d      := if 1 == 1 { c } else { 0 }
    error += if d == 100 {0} else {1}

    e: UInt := 42u
    f       := if 1 == 1 { e } else { 0u }
    error += if f == 42u {0} else {1}

    error
}

test_weak_literals_in_operators() {
    error := 0

    a: CInt := 10
    b       := a + 5
    error += if b == 15 {0} else {1}

    c: Int := 100
    d      := c + 50
    error += if d == 150 {0} else {1}

    e: UInt := 10u
    f       := e + 5u
    error += if f == 15u {0} else {1}

    error
}

test_same_type_ops() {
    error := 0
    error += test_same_type_arithmetic()
    error += test_same_type_comparison()
    error += test_same_type_conditional()
    error += test_weak_literal_in_conditional()
    error += test_weak_literals_in_operators()
    error
}

// ============================================================================
// From test_integer_standalone_cchar.tl
// ============================================================================

test_standalone_cchar() {
    error := 0
    error += if take_cchar(65) == 'A' { 0 } else { 1 }
    error
}

// ============================================================================
// From test_integer_toint.tl
// ============================================================================

test_toint() {
    // Direct to_int calls
    if (42).to_int() != 42 { c_exit(1) }
    if (true).to_int() != 1 { c_exit(2) }
    if (false).to_int() != 0 { c_exit(3) }

    // Variadic sum via while loop: single int
    if sum_while(10) != 10 { c_exit(4) }

    // Variadic sum via while loop: multiple ints
    if sum_while(1, 2, 3, 4, 5) != 15 { c_exit(5) }

    // Variadic sum via while loop: mixed types (int + bool)
    if sum_while(10, true, false, 20) != 31 { c_exit(6) }

    // Variadic sum via while loop: zero args
    if sum_while() != 0 { c_exit(7) }

    // Variadic sum via for-in loop
    if sum_for(100, 200, 300) != 600 { c_exit(8) }

    // Variadic for-in: zero args
    if sum_for() != 0 { c_exit(9) }

    // Variadic for-in: mixed types
    if sum_for(5, true, 10) != 16 { c_exit(10) }

    0
}

// ============================================================================
// From test_integer_z_literals.tl
// ============================================================================

test_z_basic() {
    error := 0

    x := 42z
    error += if x == 42z {0} else {1}

    y := 0z
    error += if y == 0z {0} else {1}

    h := 0xFFz
    error += if h == 255z {0} else {1}

    b := 0b1010z
    error += if b == 10z {0} else {1}

    c := 10z + 20z
    error += if c == 30z {0} else {1}

    error
}

test_zu_basic() {
    error := 0

    x := 42zu
    error += if x == 42zu {0} else {1}

    y := 0zu
    error += if y == 0zu {0} else {1}

    h := 0xFFzu
    error += if h == 255zu {0} else {1}

    b := 0b1010zu
    error += if b == 10zu {0} else {1}

    c := 10zu + 20zu
    error += if c == 30zu {0} else {1}

    error
}

test_z_literals_full() {
    error := 0
    error += test_z_basic()
    error += test_zu_basic()
    error
}

// ============================================================================
// Main: call all test groups and sum errors
// ============================================================================

main() {
    error := 0

    error += test_types()
    error += test_types_cast()
    error += test_cast_annotation()
    error += test_families()
    error += test_widening()
    error += test_widening_reassign()
    error += test_weak_literals()
    error += test_weak_module_const()
    error += test_weak_to_standalone()
    error += test_weak_unsigned_update()
    error += test_bounds_check()
    error += test_compound_assign()
    error += test_generic_type_args()
    error += test_narrowing_let_cast()
    error += test_same_type_ops()
    error += test_standalone_cchar()
    error += test_toint()
    error += test_z_literals_full()

    result: CInt := error
    result
}
