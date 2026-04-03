#module main
#import <cstdint.tl>

// Combined operator tests covering:
// - Binary operators (arithmetic, relational, logical, bitwise)
// - Bitwise operators
// - Bitwise compound assignment operators
// - Arithmetic compound assignment operators
// - Logical AND
// - Logical OR

test_binop() {
    error := 0

    // ── Arithmetic: + ──

    error += if 2 + 3 == 5 {0} else {1}
    error += if 0 + 0 == 0 {0} else {1}
    error += if - 1 + 1 == 0 {0} else {1}
    a := 10
    b := 20
    error += if a + b == 30 {0} else {1}

    // ── Arithmetic: - ──

    error += if 5 - 3 == 2 {0} else {1}
    error += if 3 - 5 == -2 {0} else {1}
    error += if 0 - 0 == 0 {0} else {1}
    error += if a - b == -10 {0} else {1}

    // ── Arithmetic: * ──

    error += if 3 * 4 == 12 {0} else {1}
    error += if - 3 * 4 == -12 {0} else {1}
    error += if 0 * 999 == 0 {0} else {1}
    error += if a * b == 200 {0} else {1}

    // ── Arithmetic: / ──

    error += if 10 / 2 == 5 {0} else {1}
    error += if 7 / 2 == 3 {0} else {1} // integer truncation
    error += if - 10 / 2 == -5 {0} else {1}
    error += if a / b == 0 {0} else {1} // 10 / 20 = 0
    error += if b / a == 2 {0} else {1} // 20 / 10 = 2

    // division with variables (regression: div comment bug)
    x := 100
    y := 10
    error += if x / y == 10 {0} else {1}

    // ── Arithmetic: % (modulo) ──

    error += if 10 % 3 == 1 {0} else {1}
    error += if 10 % 5 == 0 {0} else {1}
    error += if 7 % 4 == 3 {0} else {1}
    error += if a % 3 == 1 {0} else {1} // 10 % 3 = 1

    // ── Arithmetic: precedence ──

    error += if 1 + 2 * 3 == 7 {0} else {1}
    error += if (1 + 2) * 3 == 9 {0} else {1}
    error += if 10 - 2 * 3 == 4 {0} else {1}
    error += if 10 / 2 + 3 == 8 {0} else {1}
    error += if 10 % 3 + 1 == 2 {0} else {1}

    // ── Relational: == != ──

    error += if 1 == 1 {0} else {1}
    error += if 1 == 2 {1} else {0}
    error += if 1 != 2 {0} else {1}
    error += if 1 != 1 {1} else {0}
    error += if a == 10 {0} else {1}
    error += if a != b {0} else {1}

    // ── Relational: < <= > >= ──

    error += if 1 < 2 {0} else {1}
    error += if 2 < 1 {1} else {0}
    error += if 1 < 1 {1} else {0}

    error += if 1 <= 2 {0} else {1}
    error += if 1 <= 1 {0} else {1}
    error += if 2 <= 1 {1} else {0}

    error += if 2 > 1 {0} else {1}
    error += if 1 > 2 {1} else {0}
    error += if 1 > 1 {1} else {0}

    error += if 2 >= 1 {0} else {1}
    error += if 1 >= 1 {0} else {1}
    error += if 1 >= 2 {1} else {0}

    // ── Relational: with negative numbers ──

    error += if - 1 < 0 {0} else {1}
    error += if - 5 <= -5 {0} else {1}
    error += if 0 > -1 {0} else {1}
    error += if - 3 >= -3 {0} else {1}

    // ── Logical: && ──

    error += if true && true {0} else {1}
    error += if true && false {1} else {0}
    error += if false && true {1} else {0}
    error += if false && false {1} else {0}

    // ── Logical: || ──

    error += if true || true {0} else {1}
    error += if true || false {0} else {1}
    error += if false || true {0} else {1}
    error += if false || false {1} else {0}

    // ── Logical: short-circuit ──

    sc := 0
    r1 := false && ( {sc = 1 true})
    error += if sc == 0 {0} else {1}    // must not evaluate RHS

    sc2 := 0
    r2  := true || ( {sc2 = 1 true})
    error += if sc2 == 0 {0} else {1}   // must not evaluate RHS

    // ── Logical: combined ──

    error += if (1 < 2) && (3 > 1) {0} else {1}
    error += if (1 > 2) || (3 > 1) {0} else {1}
    error += if !false && true {0} else {1}
    error += if !(1 == 2) {0} else {1}

    // ── Bitwise: & | ^ ──

    error += if (0xFF & 0x0F) == 0x0F {0} else {1}
    error += if (0xF0 | 0x0F) == 0xFF {0} else {1}
    error += if (0xFF ^ 0xFF) == 0 {0} else {1}
    error += if (12 & 10) == 8 {0} else {1}  // 1100 & 1010 = 1000
    error += if (12 | 10) == 14 {0} else {1} // 1100 | 1010 = 1110
    error += if (12 ^ 10) == 6 {0} else {1}  // 1100 ^ 1010 = 0110

    // ── Bitwise: << >> ──

    error += if (1 << 0) == 1 {0} else {1}
    error += if (1 << 4) == 16 {0} else {1}
    error += if (1 << 8) == 256 {0} else {1}
    error += if (16 >> 4) == 1 {0} else {1}
    error += if (256 >> 8) == 1 {0} else {1}
    error += if (255 >> 1) == 127 {0} else {1}

    // ── Bitwise: ~ (unary, but included for completeness) ──

    u: CUInt8 := 0u
    error += if ~u == 0xFFu {0} else {1}

    // ── Bitwise: with variables ──

    m: CInt := 0x12345678
    n: CInt := 0x0F0F0F0F
    error += if (m & n) == 0x02040608 {0} else {1}
    error += if (m | n) == 0x1F3F5F7F {0} else {1}

    // ── Float arithmetic ──

    f1 := 10.0
    f2 := 3.0
    error += if f1 + f2 == 13.0 {0} else {1}
    error += if f1 - f2 == 7.0 {0} else {1}
    error += if f1 * f2 == 30.0 {0} else {1}
    error += if f1 / f2 > 3.3 && f1 / f2 < 3.4 {0} else {1}

    // ── Float relational ──

    error += if 1.0 < 2.0 {0} else {1}
    error += if 2.0 > 1.0 {0} else {1}
    error += if 1.5 <= 1.5 {0} else {1}
    error += if 1.5 >= 1.5 {0} else {1}
    error += if 1.0 == 1.0 {0} else {1}
    error += if 1.0 != 2.0 {0} else {1}

    error
}

test_bitwise() {
    error := 0

    // Test bitwise AND (&)
    // 12 = 0b1100, 10 = 0b1010, result = 0b1000 = 8
    error += if (12 & 10) == 8 {0} else {1}
    error += if (0xFF & 0x0F) == 0x0F {0} else {1}

    // Test bitwise OR (|)
    // 12 = 0b1100, 10 = 0b1010, result = 0b1110 = 14
    error += if (12 | 10) == 14 {0} else {1}
    error += if (0xF0 | 0x0F) == 0xFF {0} else {1}

    // Test bitwise XOR (^)
    // 12 = 0b1100, 10 = 0b1010, result = 0b0110 = 6
    error += if (12 ^ 10) == 6 {0} else {1}
    error += if (0xFF ^ 0xFF) == 0 {0} else {1}

    // Test bitwise NOT (~) - note: inverts all bits including sign
    x: CUInt8 := 0u
    y         := ~x
    error += if y == 0xFFu {0} else {1}

    // Test left shift (<<)
    error += if (1 << 4) == 16 {0} else {1}
    // 1 = 0b0001, result = 0b1000 = 8
    error += if (1 << 3) == 8 {0} else {1}

    // Test right shift (>>)
    error += if (16 >> 4) == 1 {0} else {1}
    // 8 = 0b1000, result = 0b0001 = 1
    error += if (8 >> 3) == 1 {0} else {1}

    // Test combinations
    // (15 & 10) | 4 = 10 | 4 = 14
    error += if ((15 & 10) | 4) == 14 {0} else {1}
    error += if ((1 << 8) - 1) == 255 {0} else {1}

    // Test with different integer types
    a: CInt := 0x12345678
    b: CInt := 0x0F0F0F0F
    error += if (a & b) == 0x02040608 {0} else {1}
    error += if (a | b) == 0x1F3F5F7F {0} else {1}

    error
}

test_bitwise_compound_assign() {
    error := 0

    // Test bitwise AND assignment (&=)
    // 0xFF & 0x0F = 0x0F
    a := 0xFF
    a &= 0x0F
    error += if a == 0x0F {0} else {1}

    // Test bitwise OR assignment (|=)
    // 0xF0 | 0x0F = 0xFF
    b := 0xF0
    b |= 0x0F
    error += if b == 0xFF {0} else {1}

    // Test bitwise XOR assignment (^=)
    // 0xFF ^ 0x0F = 0xF0
    c := 0xFF
    c ^= 0x0F
    error += if c == 0xF0 {0} else {1}

    // Test left shift assignment (<<=)
    // 1 << 4 = 16
    d := 1
    d <<= 4
    error += if d == 16 {0} else {1}

    // Test right shift assignment (>>=)
    // 16 >> 4 = 1
    e := 16
    e >>= 4
    error += if e == 1 {0} else {1}

    // Test chained operations
    f := 0xFF
    f &= 0x3F                           // 0x3F
    f |= 0x80                           // 0xBF
    f ^= 0x0F                           // 0xB0
    error += if f == 0xB0 {0} else {1}

    // Test shift chaining
    g := 1
    g <<= 8                             // 256
    g >>= 4                             // 16
    error += if g == 16 {0} else {1}

    error
}

test_compound_assignment() {
    error := 0

    // Test subtraction assignment (-=)
    x1 := 10
    x1 -= 3
    error += if x1 == 7 {0} else {1}

    x2 := 100
    x2 -= 50
    x2 -= 25
    error += if x2 == 25 {0} else {1}

    // Test multiplication assignment (*=)
    y1 := 5
    y1 *= 4
    error += if y1 == 20 {0} else {1}

    y2 := 2
    y2 *= 3
    y2 *= 4
    error += if y2 == 24 {0} else {1}

    // Test division assignment (/=)
    z1 := 20
    z1 /= 4
    error += if z1 == 5 {0} else {1}

    z2 := 100
    z2 /= 2
    z2 /= 5
    error += if z2 == 10 {0} else {1}

    // Test addition assignment (+=) for completeness
    a := 5
    a += 10
    error += if a == 15 {0} else {1}

    // Test combinations
    b := 10
    b += 5                              // 15
    b *= 2                              // 30
    b -= 10                             // 20
    b /= 4                              // 5
    error += if b == 5 {0} else {1}

    // Test with negative values
    c := 0
    c -= 5
    error += if c == -5 {0} else {1}

    // Test with floats
    f := 10.0
    f /= 4.0
    error += if f == 2.5 {0} else {1}

    error
}

test_logical_and() {
    error := 0

    // Test basic logical AND
    error += if (true && true) {0} else {1}
    error += if (true && false) {1} else {0}
    error += if (false && true) {1} else {0}
    error += if (false && false) {1} else {0}

    // Test short-circuit evaluation
    // If the first operand is false, the second should NOT be evaluated
    x      := 0
    result := false && ( {x = 1 true})
    // x should still be 0 if short-circuit worked
    error += if (0 == x) {0} else {1}

    // Test with variables
    a := true
    b := false
    error += if (a && a) {0} else {1}
    error += if (a && b) {1} else {0}
    error += if (b && a) {1} else {0}

    // Test combined with || and !
    error += if (true && true || false) {0} else {1}
    error += if (false || true && true) {0} else {1}
    error += if (!false && true) {0} else {1}
    error += if (true && !false) {0} else {1}

    // Test with comparison operators
    error += if (1 < 2 && 2 < 3) {0} else {1}
    error += if (1 == 1 && 2 == 2) {0} else {1}
    error += if (1 > 2 && 2 > 1) {1} else {0}

    // Test nested AND
    error += if (true && true && true) {0} else {1}
    error += if (true && true && false) {1} else {0}

    error
}

test_logical_or() {
    error := 0

    // Test basic logical OR
    error += if (true || true) {0} else {1}
    error += if (true || false) {0} else {1}
    error += if (false || true) {0} else {1}
    error += if (false || false) {1} else {0}

    // Test short-circuit evaluation
    // If the first operand is true, the second should NOT be evaluated
    x      := 0
    result := true || ( {x = 1 true})
    // x should still be 0 if short-circuit worked
    error += if (0 == x) {0} else {1}

    // Test with variables
    a := true
    b := false
    error += if (a || b) {0} else {1}
    error += if (b || a) {0} else {1}

    // Test combined with && and !
    error += if (false || true && true) {0} else {1}
    error += if (true && false || true) {0} else {1}
    error += if (!false || false) {0} else {1}
    error += if (false || !false) {0} else {1}

    // Test with comparison operators
    error += if (1 > 2 || 2 > 1) {0} else {1}
    error += if (1 == 1 || 1 == 2) {0} else {1}

    // Test nested OR
    error += if (false || false || true) {0} else {1}
    error += if (false || false || false) {1} else {0}

    error
}

main() {
    error := 0
    error += test_binop()
    error += test_bitwise()
    error += test_bitwise_compound_assign()
    error += test_compound_assignment()
    error += test_logical_and()
    error += test_logical_or()
    error
}
