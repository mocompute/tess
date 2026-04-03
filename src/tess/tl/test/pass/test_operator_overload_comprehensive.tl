// Comprehensive operator overload tests.
// Combined from 13 individual test files.

// --- Module: OO_Vec1 (from test_operator_overload.tl) ---

#module OO_Vec1

Vec3: { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

neg(v: Vec3) -> Vec3 {
    Vec3(x = 0 - v.x, y = 0 - v.y, z = 0 - v.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

sub(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x - b.x, y = a.y - b.y, z = a.z - b.z)
}

// --- Module: Bits (from test_operator_overload_bitwise.tl) ---

#module Bits

Mask: { val: Int }

bit_and(a: Mask, b: Mask) -> Mask { Mask(val = a.val & b.val)  }
bit_or (a: Mask, b: Mask) -> Mask { Mask(val = a.val | b.val)  }
bit_xor(a: Mask, b: Mask) -> Mask { Mask(val = a.val ^ b.val)  }
shl    (a: Mask, b: Mask) -> Mask { Mask(val = a.val << b.val) }
shr    (a: Mask, b: Mask) -> Mask { Mask(val = a.val >> b.val) }
bit_not(v: Mask)          -> Mask { Mask(val = ~v.val)         }
eq     (a: Mask, b: Mask) -> Bool { a.val == b.val             }

// --- Module: Mat (from test_operator_overload_chain.tl) ---

#module Mat

Mat1: { val: Int }

add(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val + b.val) }
sub(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val - b.val) }
mul(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val * b.val) }
neg(v: Mat1)          -> Mat1 { Mat1(val = 0 - v.val)     }
eq (a: Mat1, b: Mat1) -> Bool { a.val == b.val            }

cmp(a: Mat1, b: Mat1) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}

// --- Module: Bar (from test_operator_overload_cmp_ptr_const.tl) ---

#module Bar

Bar: { v: Int }

cmp(a: Ptr[Const[Bar]], b: Ptr[Const[Bar]]) -> CInt {
    if a->v < b->v { -1 }
    else if a->v > b->v { 1 }
    else { 0 }
}

// --- Module: OO_Vec2 (from test_operator_overload_compound.tl) ---

#module OO_Vec2

Vec3: { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

sub(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x - b.x, y = a.y - b.y, z = a.z - b.z)
}

mul(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x * b.x, y = a.y * b.y, z = a.z * b.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

cmp(a: Vec3, b: Vec3) -> CInt {
    // Compare lexicographically: x, then y, then z
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else if a.z < b.z { -1 }
    else if a.z > b.z { 1 }
    else { 0 }
}

// --- Module: OO_Num1 (from test_operator_overload_div_mod.tl) ---

#module OO_Num1

Number: { val: Int }

div(a: Number, b: Number) -> Number { Number(val = a.val / b.val) }
mod(a: Number, b: Number) -> Number { Number(val = a.val % b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }

// --- Module: Val (from test_operator_overload_eq_from_cmp.tl) ---

#module Val

Value: { n: Int }

// Only cmp -- no eq function
cmp(a: Value, b: Value) -> CInt {
    if a.n < b.n { -1 }
    else if a.n > b.n { 1 }
    else { 0 }
}

// --- Module: Pair (from test_operator_overload_eq_only.tl) ---

#module Pair

Pair: { first: Int, second: Int }

eq(a: Pair, b: Pair) -> Bool {
    a.first == b.first && a.second == b.second
}

// --- Module: Foo (from test_operator_overload_eq_ptr_const.tl) ---

#module Foo

Foo: { v: Int }

eq(a: Ptr[Const[Foo]], b: Ptr[Const[Foo]]) -> Bool {
    a->v == b->v
}

// --- Module: OO_Num2 (from test_operator_overload_in_function.tl) ---

#module OO_Num2

Number:                             { val: Int                    }
eq(a: Number, b: Number) -> Bool    { a.val == b.val              }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }

// --- Module: OO_Vec3 (from test_operator_overload_multi.tl) ---

#module OO_Vec3

Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
sub(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x - b.x, y = a.y - b.y) }
mul(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x * b.x, y = a.y * b.y) }
neg(v: Vec2)          -> Vec2 { Vec2(x = 0 - v.x, y = 0 - v.y)     }
eq (a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y           }

// --- Module: Color (from test_operator_overload_multi.tl) ---

#module Color

RGB: { r: Int, g: Int, b: Int }

add(a: RGB, b: RGB) -> RGB  { RGB(r = a.r + b.r, g = a.g + b.g, b = a.b + b.b) }
eq (a: RGB, b: RGB) -> Bool { a.r == b.r && a.g == b.g && a.b == b.b           }

// --- Module: Opt (from test_operator_overload_tagged_union.tl) ---

#module Opt

OptInt: | Val { v: Int } | Empty

eq(a: OptInt, b: OptInt) -> Bool {
    when a {
        sa: Val {
            sb: Val := b else { return false }
            sa.v == sb.v
        }
        na: Empty {
            when b {
                nb: Empty { true }
                else { false }
            }
        }
    }
}

add(a: OptInt, b: OptInt) -> OptInt {
    sa: Val := a else { return OptInt.Empty }
    sb: Val := b else { return OptInt.Empty }
    OptInt.Val(v = sa.v + sb.v)
}

// --- Module: Logic (from test_operator_overload_unary.tl) ---

#module Logic

Flag: { val: Bool }

not(a: Flag)          -> Bool { !a.val         }
eq (a: Flag, b: Flag) -> Bool { a.val == b.val }

// --- Module: main ---

#module main

// --- test_operator_overload ---
test_operator_overload() {
    error := 0

    a := OO_Vec1.Vec3(x = 1, y = 2, z = 3)
    b := OO_Vec1.Vec3(x = 4, y = 5, z = 6)

    // binary + dispatches to OO_Vec1.add
    c := a + b
    error += if c.x == 5 && c.y == 7 && c.z == 9 {0} else {1}

    // binary - dispatches to OO_Vec1.sub
    d := a - b
    error += if d.x == 0 - 3 && d.y == 0 - 3 && d.z == 0 - 3 {0} else {1}

    // unary - dispatches to OO_Vec1.neg
    e := -a
    error += if e.x == 0 - 1 && e.y == 0 - 2 && e.z == 0 - 3 {0} else {1}

    // == dispatches to OO_Vec1.eq
    error += if a == a {0} else {1}
    error += if a == b {1} else {0}

    // != is derived from eq (negated)
    error += if a != b {0} else {1}
    error += if a != a {1} else {0}

    error
}

// --- test_operator_overload_bitwise ---
test_bitwise() {
    error := 0

    a := Bits.Mask(val = 0xFF)
    b := Bits.Mask(val = 0x0F)

    // & dispatches to Bits.bit_and
    c := a & b
    error += if c.val == 0x0F {0} else {1}

    // | dispatches to Bits.bit_or
    d := a | b
    error += if d.val == 0xFF {0} else {1}

    // ^ dispatches to Bits.bit_xor
    e := a ^ b
    error += if e.val == 0xF0 {0} else {1}

    // << dispatches to Bits.shl
    one  := Bits.Mask(val = 1)
    four := Bits.Mask(val = 4)
    f    := one << four
    error += if f.val == 16 {0} else {1}

    // >> dispatches to Bits.shr
    sixteen := Bits.Mask(val = 16)
    g       := sixteen >> four
    error += if g.val == 1 {0} else {1}

    // ~ dispatches to Bits.bit_not
    zero := Bits.Mask(val = 0)
    h    := ~zero
    error += if h.val == ~0 {0} else {1}

    // Compound assignment &=
    x := Bits.Mask(val = 0xFF)
    x &= b
    error += if x.val == 0x0F {0} else {1}

    // Compound assignment |=
    y := Bits.Mask(val = 0xF0)
    y |= b
    error += if y.val == 0xFF {0} else {1}

    // Compound assignment ^=
    z := Bits.Mask(val = 0xFF)
    z ^= b
    error += if z.val == 0xF0 {0} else {1}

    // Compound assignment <<=
    s := Bits.Mask(val = 1)
    s <<= four
    error += if s.val == 16 {0} else {1}

    // Compound assignment >>=
    t := Bits.Mask(val = 16)
    t >>= four
    error += if t.val == 1 {0} else {1}

    error
}

// --- test_operator_overload_chain ---
test_chain() {
    error := 0

    a := Mat.Mat1(val = 2)
    b := Mat.Mat1(val = 3)
    c := Mat.Mat1(val = 5)

    // Chained addition: a + b + c
    r1 := a + b + c
    error += if r1.val == 10 {0} else {1}

    // Chained subtraction: c - b - a
    r2 := c - b - a
    error += if r2.val == 0 {0} else {1}

    // Mixed operators: a * b + c
    r3 := a * b + c
    error += if r3.val == 11 {0} else {1}

    // Double negation
    r4 := -(-a)
    error += if r4 == a {0} else {1}

    // Compound assignments chained
    acc := Mat.Mat1(val = 0)
    acc += a
    acc += b
    acc += c
    error += if acc.val == 10 {0} else {1}

    // Comparison operators chained with logic
    error += if a < b && b < c {0} else {1}
    error += if c > b && b > a {0} else {1}
    error += if a <= a && a >= a {0} else {1}

    // Equality in conditional
    x := if a == a { Mat.Mat1(val = 100) } else { Mat.Mat1(val = 0) }
    error += if x.val == 100 {0} else {1}

    error
}

// --- test_operator_overload_cmp_ptr_const ---
test_cmp_ptr_const() {
    a := Bar(v = 1)
    b := Bar(v = 2)
    c := Bar(v = 1)

    r := 0

    if !(a < b) { r = r + 1 }
    if !(b > a) { r = r + 1 }
    if !(a <= c) { r = r + 1 }
    if !(a >= c) { r = r + 1 }
    if !(a == c) { r = r + 1 }
    if !(a != b) { r = r + 1 }

    r
}

// --- test_operator_overload_compound ---
test_compound() {
    error := 0

    a := OO_Vec2.Vec3(x = 1, y = 2, z = 3)
    b := OO_Vec2.Vec3(x = 4, y = 5, z = 6)

    // Compound assignment += dispatches to OO_Vec2.add
    c := a
    c += b
    error += if c.x == 5 && c.y == 7 && c.z == 9 {0} else {1}

    // Compound assignment -= dispatches to OO_Vec2.sub
    d := b
    d -= a
    error += if d.x == 3 && d.y == 3 && d.z == 3 {0} else {1}

    // Compound assignment *= dispatches to OO_Vec2.mul
    e := OO_Vec2.Vec3(x = 2, y = 3, z = 4)
    f := OO_Vec2.Vec3(x = 5, y = 6, z = 7)
    e *= f
    error += if e.x == 10 && e.y == 18 && e.z == 28 {0} else {1}

    // Derived comparison < via cmp
    lo := OO_Vec2.Vec3(x = 1, y = 2, z = 3)
    hi := OO_Vec2.Vec3(x = 2, y = 0, z = 0)
    error += if lo < hi {0} else {1}
    error += if hi < lo {1} else {0}

    // Derived comparison <= via cmp
    error += if lo <= hi {0} else {1}
    error += if lo <= lo {0} else {1}
    error += if hi <= lo {1} else {0}

    // Derived comparison > via cmp
    error += if hi > lo {0} else {1}
    error += if lo > hi {1} else {0}

    // Derived comparison >= via cmp
    error += if hi >= lo {0} else {1}
    error += if lo >= lo {0} else {1}
    error += if lo >= hi {1} else {0}

    error
}

// --- test_operator_overload_div_mod ---
test_div_mod() {
    error := 0

    a := OO_Num1.Number(val = 17)
    b := OO_Num1.Number(val = 5)

    // / dispatches to OO_Num1.div
    c := a / b
    error += if c.val == 3 {0} else {1}

    // % dispatches to OO_Num1.mod
    d := a % b
    error += if d.val == 2 {0} else {1}

    // Compound assignment /=
    e := OO_Num1.Number(val = 20)
    e /= b
    error += if e.val == 4 {0} else {1}

    // Compound assignment %=
    f := OO_Num1.Number(val = 17)
    f %= b
    error += if f.val == 2 {0} else {1}

    error
}

// --- test_operator_overload_eq_from_cmp ---
test_eq_from_cmp() {
    error := 0

    a := Val.Value(n = 5)
    b := Val.Value(n = 5)
    c := Val.Value(n = 10)

    // == should derive from cmp(a, b) == 0
    error += if a == b {0} else {1}
    error += if a == c {1} else {0}

    // != should derive from cmp(a, b) != 0
    error += if a != c {0} else {1}
    error += if a != b {1} else {0}

    // < should still work
    error += if a < c {0} else {1}

    error
}

// --- test_operator_overload_eq_only ---
test_eq_only() {
    error := 0

    a := Pair.Pair(first = 1, second = 2)
    b := Pair.Pair(first = 1, second = 2)
    c := Pair.Pair(first = 3, second = 4)

    // == works
    error += if a == b {0} else {1}
    error += if a == c {1} else {0}

    // != works (derived from eq)
    error += if a != c {0} else {1}
    error += if a != b {1} else {0}

    // Self-equality
    error += if a == a {0} else {1}
    error += if a != a {1} else {0}

    error
}

// --- test_operator_overload_eq_ptr_const ---
test_eq_ptr_const() {
    f1 := Foo(v = 1)
    f2 := Foo(v = 1)
    f3 := Foo(v = 2)

    r := 0

    // eq: same values
    if !(f1 == f2) { r = r + 1 }

    // eq: different values
    if f1 == f3 { r = r + 1 }

    // neq: different values
    if !(f1 != f3) { r = r + 1 }

    // neq: same values
    if f1 != f2 { r = r + 1 }

    r
}

// --- test_operator_overload_in_function ---
// Helper functions for in_function test
infn_test_eq() {
    a     := OO_Num2.Number(val = 1)
    b     := OO_Num2.Number(val = 1)
    c     := OO_Num2.Number(val = 2)
    error := 0
    error += if a == b { 0 } else { 1 }
    error += if a == c { 1 } else { 0 }
    error
}

infn_test_add() {
    a := OO_Num2.Number(val = 3)
    b := OO_Num2.Number(val = 4)
    c := a + b
    if c.val == 7 { 0 } else { 1 }
}

test_in_function() {
    error := 0
    error += infn_test_eq()
    error += infn_test_add()
    error
}

// --- test_operator_overload_multi ---
test_multi() {
    error := 0

    // Test Vec2 mul operator
    a := OO_Vec3.Vec2(x = 3, y = 4)
    b := OO_Vec3.Vec2(x = 2, y = 5)
    c := a * b
    error += if c.x == 6 && c.y == 20 {0} else {1}

    // Test chained operations: (a + b) - a == b
    d := a + b - a
    error += if d == b {0} else {1}

    // Test negation then add: -a + a == zero
    zero := OO_Vec3.Vec2(x = 0, y = 0)
    e    := -a + a
    error += if e == zero {0} else {1}

    // Test different type (Color) in same program
    red   := Color.RGB(r = 255, g = 0, b = 0)
    green := Color.RGB(r = 0, g = 128, b = 0)
    mixed := red + green
    error += if mixed.r == 255 && mixed.g == 128 && mixed.b == 0 {0} else {1}

    // Test equality on Color
    error += if red == red {0} else {1}
    error += if red != green {0} else {1}

    error
}

// --- test_operator_overload_tagged_union ---
test_tagged_union() {
    error := 0

    a := Opt.OptInt.Val(v = 10)
    b := Opt.OptInt.Val(v = 20)
    n := Opt.OptInt.Empty

    // == on tagged union
    error += if a == a {0} else {1}
    error += if a == b {1} else {0}
    error += if n == n {0} else {1}
    error += if a == n {1} else {0}

    // != derived from eq
    error += if a != b {0} else {1}
    error += if a != a {1} else {0}

    // + on tagged union
    c := a + b
    error += if c == Opt.OptInt.Val(v = 30) {0} else {1}

    // + with None propagates
    d := a + n
    error += if d == n {0} else {1}

    error
}

// --- test_operator_overload_unary ---
test_unary() {
    error := 0

    t := Logic.Flag(val = 1 == 1)
    f := Logic.Flag(val = 1 == 0)

    // ! dispatches to Logic.not
    error += if !t { 1 } else { 0 }
    error += if !f { 0 } else { 1 }

    error
}

// --- main: run all tests ---
main() {
    error := 0
    error += test_operator_overload()
    error += test_bitwise()
    error += test_chain()
    error += test_cmp_ptr_const()
    error += test_compound()
    error += test_div_mod()
    error += test_eq_from_cmp()
    error += test_eq_only()
    error += test_eq_ptr_const()
    error += test_in_function()
    error += test_multi()
    error += test_tagged_union()
    error += test_unary()
    error
}
