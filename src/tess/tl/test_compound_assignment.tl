#module main

main() {
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
