#module main
#import <cstdint.tl>

// --- z literal suffix (signed, CPtrDiff-like) ---

test_z_basic() {
    error := 0

    x := 42z
    error += if x == 42z {0} else {1}

    // Zero
    y := 0z
    error += if y == 0z {0} else {1}

    // Hex
    h := 0xFFz
    error += if h == 255z {0} else {1}

    // Binary
    b := 0b1010z
    error += if b == 10z {0} else {1}

    // Arithmetic
    c := 10z + 20z
    error += if c == 30z {0} else {1}

    error
}

// --- zu literal suffix (unsigned, CSize-like) ---

test_zu_basic() {
    error := 0

    x := 42zu
    error += if x == 42zu {0} else {1}

    // Zero
    y := 0zu
    error += if y == 0zu {0} else {1}

    // Hex
    h := 0xFFzu
    error += if h == 255zu {0} else {1}

    // Binary
    b := 0b1010zu
    error += if b == 10zu {0} else {1}

    // Arithmetic
    c := 10zu + 20zu
    error += if c == 30zu {0} else {1}

    error
}

// --- Main ---

main() {
    error := 0

    error += test_z_basic()
    error += test_zu_basic()

    error
}
