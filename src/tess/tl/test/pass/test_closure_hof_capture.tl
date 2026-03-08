#module main
#import <stdlib.tl>

// Test passing a capturing closure to a higher-order function.
// This was impossible before the unified tl_Closure convention:
// the HOF expected a bare function pointer but the closure had a
// different signature (typed context struct as first param).

apply(f, x)  { f(x)  }
apply0(f)    { f()    }

main() {
    offset := 10

    // Capturing lambda passed to a HOF
    add_offset := (x) { x + offset }
    result := apply(add_offset, 32)
    c_assert(result == 42)

    // Zero-capture lambda passed to a HOF
    double := (x) { x + x }
    result2 := apply(double, 21)
    c_assert(result2 == 42)

    // Nullary capturing closure passed to a HOF
    get_offset := () { offset }
    result3 := apply0(get_offset)
    c_assert(result3 == 10)

    // Named (non-lambda) function passed to a HOF
    result4 := apply(add_offset, -10 + offset)
    c_assert(result4 == 10)

    0
}
