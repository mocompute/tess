#module main
#import <stdint.tl>

// Comprehensive binary operator tests.
// Covers arithmetic, relational, logical, and bitwise operators
// with both literals and variables.

main() {
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
