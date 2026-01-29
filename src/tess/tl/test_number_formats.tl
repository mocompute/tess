#module main

main() {
    error := 0

    // Test scientific notation with negative exponent
    f1 := 1e-2
    error += if f1 == 0.01 {0} else {1}

    f2 := 1.5e-3
    error += if f2 == 0.0015 {0} else {1}

    f3 := 2.5E-1
    error += if f3 == 0.25 {0} else {1}

    // Test scientific notation with explicit positive exponent
    f4 := 1e+2
    error += if f4 == 100.0 {0} else {1}

    f5 := 1.5e+3
    error += if f5 == 1500.0 {0} else {1}

    f6 := 2.5E+1
    error += if f6 == 25.0 {0} else {1}

    // Test integer with negative exponent (becomes float)
    f7 := 5e-1
    error += if f7 == 0.5 {0} else {1}

    // Test hex numbers still work (e is a hex digit)
    h1 := 0xABCDef
    error += if h1 == 11259375 {0} else {1}

    h2 := 0xE0
    error += if h2 == 224 {0} else {1}

    // Test binary literals
    b1 := 0b1010
    error += if b1 == 10 {0} else {1}

    b2 := 0B1111
    error += if b2 == 15 {0} else {1}

    b3 := 0b1111_0000
    error += if b3 == 240 {0} else {1}

    b4 := 0b0
    error += if b4 == 0 {0} else {1}

    // Binary with many digits
    b5 := 0b11111111
    error += if b5 == 255 {0} else {1}

    error
}
