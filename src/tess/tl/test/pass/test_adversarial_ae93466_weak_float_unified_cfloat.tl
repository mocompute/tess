#module main

// Test: weak float literal in a generic context where the return type
// constrains it to CFloat. The weak float defaulting must preserve the
// already-unified CFloat rather than overwriting it with the default
// Float (= CDouble).

make_result_float(x: CFloat) -> Result[CFloat, CInt] {
    Ok(x)
}

// Float literal 1.5 goes through Result[CFloat, CInt] — it must unify
// with CFloat, not get defaulted to CDouble.
test_result_cfloat() -> CInt {
    r := make_result_float(1.5)
    when r {
        ok: Ok {
            // sizeof should match CFloat, not CDouble
            if sizeof(ok.value) != sizeof[CFloat]() { return 1 }
            if ok.value != 1.5 { return 1 }
            0
        }
        err: Err { 1 }
    }
}

// Float literal in a generic pair where one element constrains the type
Pair[A, B]: { a: A, b: B }

make_float_pair(x: CFloat) -> Pair[CFloat, CFloat] {
    Pair(a = x, b = 2.5)
}

test_pair_cfloat() -> CInt {
    p     := make_float_pair(1.0)
    error := 0
    error += if sizeof(p.a) == sizeof[CFloat]() { 0 } else { 1 }
    error += if sizeof(p.b) == sizeof[CFloat]() { 0 } else { 1 }
    error += if p.b != 2.5 { 1 } else { 0 }
    error
}

main() {
    error := 0
    error += test_result_cfloat()
    error += test_pair_cfloat()
    error
}
