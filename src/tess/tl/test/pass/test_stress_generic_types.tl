#module main

// Generic tagged union containing another generic tagged union (defined bottom-up)
Inner[a]: | IHas { value: a }
          | IEmpty

Outer[a]: | OHas { inner: Inner[a] }
          | OEmpty

test_nested_generic_unions() {
    x: Outer[Int] := OHas(IHas(42))

    result := when x {
        oh: OHas {
            when oh.inner {
                ih: IHas { ih.value }
                ie: IEmpty { -1 }
            }
        }
        oe: OEmpty { -2 }
    }
    if result == 42 {0} else {1}
}

// Same but with Float type parameter
test_nested_generic_unions_float() {
    x: Outer[Float] := OHas(IHas(3.14))

    result := when x {
        oh: OHas {
            when oh.inner {
                ih: IHas { ih.value }
                ie: IEmpty { 0.0 }
            }
        }
        oe: OEmpty { 0.0 }
    }
    if result > 3.0 {0} else {1}
}

// Generic function returning a generic tagged union
wrap[a](v: a) -> Inner[a] {
    IHas(v)
}

test_generic_func_returning_union() {
    x      := wrap(99)
    result := when x {
        ih: IHas { ih.value }
        ie: IEmpty { 0 }
    }
    if result == 99 {0} else {1}
}

// Multiple type parameters with constraints through usage
Either[a, b]: | Left  { v: a }
              | Right { v: b }

swap_either[a, b](e: Either[a, b]) -> Either[b, a] {
    when e {
        l: Left { Right(l.v) }
        r: Right { Left(r.v) }
    }
}

test_multi_type_params() {
    x: Either[Int, Float] := Left(10)
    y                     := swap_either(x)

    result := when y {
        l: Left { 0 }                   // Float
        r: Right { r.v }                // Int
    }
    if result == 10 {0} else {1}
}

// Generic unwrap function
my_unwrap[a](opt: Inner[a], default: a) -> a {
    when opt {
        ih: IHas { ih.value }
        ie: IEmpty { default }
    }
}

test_generic_unwrap_chain() {
    a             := wrap(100)
    b: Inner[Int] := IEmpty()

    r1 := my_unwrap(a, 0)
    r2 := my_unwrap(b, -1)

    error := 0
    error += if r1 == 100 {0} else {1}
    error += if r2 == -1 {0} else {1}
    error
}

// when on a generic tagged union where the type param is itself a tagged union
test_type_param_is_union() {
    // Inner[Inner[Int]] - type param is itself a tagged union
    nested: Inner[Inner[Int]] := IHas(IHas(77))

    result := when nested {
        outer: IHas {
            when outer.value {
                inner: IHas { inner.value }
                ie:    IEmpty { 0 }
            }
        }
        oe: IEmpty { 0 }
    }
    if result == 77 {0} else {1}
}

main() {
    error := 0
    error += test_nested_generic_unions()
    error += test_nested_generic_unions_float()
    error += test_generic_func_returning_union()
    error += test_multi_type_params()
    error += test_generic_unwrap_chain()
    error += test_type_param_is_union()
    error
}
