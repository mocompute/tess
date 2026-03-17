#module main

// Runtime failure test: float narrowing overflow.
// When compiled with --bounds-check, narrowing a CDouble value that
// exceeds CFloat range must abort (finite value becomes infinity).
// CFloat max is approximately 3.4e38; we build a value beyond that.

big() -> CDouble {
    // 4.0e38 > CFloat max (3.4e38)
    x: CDouble := 4.0
    x = x * 10000000000.0
    x = x * 10000000000.0
    x = x * 10000000000.0
    x = x * 1000000000.0
    x
}

main() {
    d            := big()
    f:      CFloat    := d
    result: CInt := f
    result
}
