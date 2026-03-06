#module main
#import <stdint.tl>

// Test that different integer types produce distinct generic specializations.
// Array[CInt] and Array[Int] should be separate types.

wrap[T](x: T) -> T { x }

test_distinct_specializations() {
    error := 0

    // wrap with CInt vs wrap with Int — distinct specializations
    a: CInt := 42
    b: Int := 99
    ra := wrap(a)
    rb := wrap(b)
    error += if ra == 42 {0} else {1}
    error += if rb == 99 {0} else {1}

    // wrap with CInt8 vs wrap with CInt32
    c: CInt8 := 10
    d: CInt32 := 1000
    rc := wrap(c)
    rd := wrap(d)
    error += if rc == 10 {0} else {1}
    error += if rd == 1000 {0} else {1}

    // wrap with unsigned variants
    e: UInt := 42u
    f: CUInt8 := 200u
    re := wrap(e)
    rf := wrap(f)
    error += if re == 42u {0} else {1}
    error += if rf == 200u {0} else {1}

    error
}

// Generic pair that preserves both types
pair_first[T, U](x: T, y: U) -> T { x }

test_generic_pair() {
    error := 0

    a: CInt := 42
    b: Int := 99
    result := pair_first(a, b)
    error += if result == 42 {0} else {1}

    c: CInt8 := 10
    d: UInt := 50u
    result2 := pair_first(c, d)
    error += if result2 == 10 {0} else {1}

    error
}

main() {
    error := 0
    error += test_distinct_specializations()
    error += test_generic_pair()
    error
}
