#module main
#import <cstdio.tl>

// Helper functions (shared by test_weak_literal and test_widening)
take_cfloat     (x: CFloat)      -> CFloat      { x }
take_cdouble    (x: CDouble)     -> CDouble     { x }
take_clongdouble(x: CLongDouble) -> CLongDouble { x }

// Helper function for test_widening
widen_return() -> CDouble {
    f: CFloat := 1.5
    f
}

// From test_float_cast.tl
// Float narrowing via let-in annotation, float-integer cast.
test_cast() {
    error := 0

    // Float narrowing: CDouble -> CFloat via let-in annotation
    d:      CDouble := 1.5
    narrow: CFloat  := d
    error += if sizeof(narrow) == sizeof[CFloat]() {0} else {1}

    // Float to integer cast
    f: Float := 3.0
    i: Int   := f
    error += if i == 3 {0} else {1}

    // Integer to float cast
    n:  Int   := 42
    fl: Float := n
    error += if fl == 42.0 {0} else {1}

    // CFloat to CInt cast
    cf: CFloat := 7.0
    ci: CInt   := cf
    error += if ci == 7 {0} else {1}

    error
}

// From test_float_same_type.tl
// Same-type float operators, comparisons, and conditionals.
test_same_type() {
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

// From test_float_types.tl
// Float type sizes and widening assignments.
test_types() {
    error := 0

    f: CFloat := 0.0
    error += if sizeof[CFloat]() < sizeof[CDouble]() {0} else {1}

    d: CDouble := f
    error += if sizeof(d) == sizeof[CDouble]() {0} else {1}

    // On macOS, sizeof long double == sizeof double
    ld: CLongDouble := f
    error += if sizeof(ld) >= sizeof[CDouble]() {0} else {1}

    c_printf(c"sizeof CFloat = %u\n", sizeof[CFloat]())
    c_printf(c"sizeof CDouble = %u\n", sizeof[CDouble]())
    c_printf(c"sizeof CLongDouble = %u\n", sizeof[CLongDouble]())

    error
}

// From test_float_weak_literal.tl
// Weak float literal resolves to CFloat from context.
test_weak_literal() {
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

// From test_float_widening.tl
// CFloat -> CDouble and CFloat -> CLongDouble widening in funcalls and returns.
test_widening() {
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

main() {
    error := 0
    error += test_cast()
    error += test_same_type()
    error += test_types()
    error += test_weak_literal()
    error += test_widening()
    error
}
