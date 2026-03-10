#module main

main() {
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
