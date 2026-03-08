#module main

// Test passing a capturing closure to a higher-order function.
// This was impossible before the unified tl_closure convention:
// the HOF expected a bare function pointer but the closure had a
// different signature (typed context struct as first param).

apply (f, x) { f(x) }
apply0(f)    { f()  }

main() {
    error  := 0
    offset := 10

    // Capturing lambda passed to a HOF
    add_offset := (x) { x + offset }
    error += if apply(add_offset, 32) == 42 {0} else {1}

    // Zero-capture lambda passed to a HOF
    double := (x) { x + x }
    error += if apply(double, 21) == 42 {0} else {1}

    // Nullary capturing closure passed to a HOF
    get_offset := () { offset }
    error += if apply0(get_offset) == 10 {0} else {1}

    // Named (non-lambda) function passed to a HOF
    error += if apply(add_offset, -10 + offset) == 10 {0} else {1}

    error
}
