#module main
#import <stdio.tl>

// Test: weak float literal resolves to CFloat from context.

take_cfloat     (x: CFloat)      -> CFloat      { x }
take_cdouble    (x: CDouble)     -> CDouble     { x }
take_clongdouble(x: CLongDouble) -> CLongDouble { x }

main() {
    error := 0

    // Literal resolves to CFloat via function parameter context
    f := take_cfloat(3.14)
    error += if sizeof(f) == sizeof[CFloat]() {0} else {1}

    // Literal resolves to CDouble via function parameter context
    d := take_cdouble(3.14)
    error += if sizeof(d) == sizeof[CDouble]() {0} else {1}

    // Literal resolves to CLongDouble via function parameter context
    ld := take_clongdouble(3.14)
    error += if sizeof(ld) >= sizeof[CDouble]() {0} else {1}

    // Uncontextualized literal defaults to Float (= CDouble)
    x := 3.14
    error += if sizeof(x) == sizeof[CDouble]() {0} else {1}

    error
}
