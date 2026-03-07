#module main

main() {
    error := 0

    // ── Scientific notation: negative exponent ──

    f1 := 1e-2
    error += if f1 == 0.01 {0} else {1}

    f2 := 1.5e-3
    error += if f2 == 0.0015 {0} else {1}

    f3 := 2.5E-1
    error += if f3 == 0.25 {0} else {1}

    // ── Scientific notation: explicit positive exponent ──

    f4 := 1e+2
    error += if f4 == 100.0 {0} else {1}

    f5 := 1.5e+3
    error += if f5 == 1500.0 {0} else {1}

    f6 := 2.5E+1
    error += if f6 == 25.0 {0} else {1}

    // ── Scientific notation: integer with negative exponent ──

    f7 := 5e-1
    error += if f7 == 0.5 {0} else {1}

    // ── Scientific notation: positive exponent (no sign) ──

    f8 := 1.5e10
    error += if f8 == 15000000000.0 {0} else {1}

    f9 := 2.5E3
    error += if f9 == 2500.0 {0} else {1}

    f10 := 1e6
    error += if f10 == 1000000.0 {0} else {1}

    f11 := -3.14e2
    error += if f11 == -314.0 {0} else {1}

    f12 := 6.022e23
    error += if f12 > 0.0 {0} else {1}

    f13 := 1.234e1
    error += if f13 == 12.34 {0} else {1}

    // ── Hex literals ──

    h1 := 0xABCDef
    error += if h1 == 11259375 {0} else {1}

    h2 := 0xE0
    error += if h2 == 224 {0} else {1}

    error += if 0x10 == 16 {0} else {1}
    error += if 0xff == 255 {0} else {1}
    error += if 0xABCD == 43981 {0} else {1}

    // Uppercase X prefix
    error += if 0X10 == 16 {0} else {1}
    error += if 0XFF == 255 {0} else {1}

    // Hex with underscores
    error += if 0xFF_FF == 0xFFFF {0} else {1}

    // ── Binary literals ──

    b1 := 0b1010
    error += if b1 == 10 {0} else {1}

    b2 := 0B1111
    error += if b2 == 15 {0} else {1}

    b3 := 0b1111_0000
    error += if b3 == 240 {0} else {1}

    b4 := 0b0
    error += if b4 == 0 {0} else {1}

    b5 := 0b11111111
    error += if b5 == 255 {0} else {1}

    error += if 0b1 == 1 {0} else {1}
    error += if 0B1010 == 10 {0} else {1}
    error += if 0B11111111 == 255 {0} else {1}
    error += if 0b1010_1010 == 170 {0} else {1}

    // ── Octal literals ──

    error += if 010 == 8 {0} else {1}
    error += if 0377 == 255 {0} else {1}
    error += if 0644 == 420 {0} else {1}

    // ── Decimal literals ──

    error += if 42 == 42 {0} else {1}
    error += if 1000000 == 1000000 {0} else {1}

    // Decimal with underscores
    error += if 1_000_000 == 1000000 {0} else {1}

    // ── Negative base values ──

    error += if - 0x10 == -16 {0} else {1}
    error += if - 010 == -8 {0} else {1}

    // ── Mixed base comparison ──

    error += if 0x100 == 256 {0} else {1}
    error += if 0400 == 256 {0} else {1}

    error
}
