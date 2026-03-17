#module main
#import <cstdio.tl>

// Test: CFloat -> CDouble and CFloat -> CLongDouble widening in funcalls and returns.

take_cdouble    (x: CDouble)     -> CDouble         { x }
take_clongdouble(x: CLongDouble) -> CLongDouble { x     }

widen_return() -> CDouble {
    f: CFloat := 1.5
    f
}

main() {
    error := 0

    f: CFloat := 1.5

    // Widening via function call
    d := take_cdouble(f)
    error += if d == 1.5 {0} else {1}

    ld := take_clongdouble(f)
    error += if ld == 1.5 {0} else {1}

    // CDouble -> CLongDouble widening
    d2: CDouble := 2.5
    ld2         := take_clongdouble(d2)
    error += if ld2 == 2.5 {0} else {1}

    // Widening via return
    r := widen_return()
    error += if r == 1.5 {0} else {1}

    error
}
