#module main

// Test: same-type float operators, comparisons, and conditionals.

main() {
    error := 0

    // Same-type arithmetic
    a: CFloat := 1.5
    b: CFloat := 2.5
    c         := a + b
    error += if c == 4.0 {0} else {1}

    d: CDouble := 10.0
    e: CDouble := 3.0
    f          := d - e
    error += if f == 7.0 {0} else {1}

    // Same-type comparison
    error += if a < b {0} else {1}
    error += if d > e {0} else {1}

    // Same-type conditional
    r := if 1 == 1 { a } else { b }
    error += if r == 1.5 {0} else {1}

    error
}
