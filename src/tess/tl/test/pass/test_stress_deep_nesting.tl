#module main

// Deep nesting stress test

// Tagged unions for nested when tests (defined bottom-up for forward reference)
Inner: | IA { value: Int }
       | IB

Middle: | MA { inner: Inner }
        | MB

Outer: | OA { inner: Middle }
       | OB

// 4+ levels of nested when
test_nested_when() {
    x := OA(MA(IA(42)))

    result := when x {
        oa: OA {
            when oa.inner {
                ma: MA {
                    when ma.inner {
                        ia: IA { ia.value }
                        ib: IB { -1 }
                    }
                }
                mb: MB { -2 }
            }
        }
        ob: OB { -3 }
    }
    if result == 42 {0} else {1}
}

// Nested when with different tagged union types at each level
Color: | Red | Green | Blue

test_nested_when_different_types() {
    x := OA(MA(IA(10)))
    c := Green()

    result := when x {
        oa: OA {
            when oa.inner {
                ma: MA {
                    val := when ma.inner {
                        ia: IA { ia.value }
                        ib: IB { 0 }
                    }
                    when c {
                        r: Red   { val + 1 }
                        g: Green { val + 2 }
                        b: Blue  { val + 3 }
                    }
                }
                mb: MB { 0 }
            }
        }
        ob: OB { 0 }
    }
    if result == 12 {0} else {1}
}

// Deeply nested if/else chains (6+ levels)
test_deep_if_else() {
    a := 1
    b := 2
    c := 3
    d := 4
    e := 5
    f := 6

    result := if a == 1 {
        if b == 2 {
            if c == 3 {
                if d == 4 {
                    if e == 5 {
                        if f == 6 {
                            100
                        } else { 0 }
                    } else { 0 }
                } else { 0 }
            } else { 0 }
        } else { 0 }
    } else { 0 }

    if result == 100 {0} else {1}
}

// Nested block expressions 4+ levels deep
test_nested_blocks() {
    result := ({
        a := ({
            b := ({
                c := ({
                    10 + 20
                })
                c + 5
            })
            b * 2
        })
        a - 70
    })
    if result == 0 {0} else {1}
}

// Nested let-in bindings 5+ levels deep
test_nested_let_in() {
    result := (a := 1
               (b := a + 1
                (c := b + 1
                 (d := c + 1
                  (e := d + 1
                   a + b + c + d + e)))))
    // 1 + 2 + 3 + 4 + 5 = 15
    if result == 15 {0} else {1}
}

// Deep nesting combining when + if + block
test_mixed_deep_nesting() {
    x := OA(MA(IA(7)))

    result := when x {
        oa: OA {
            if 1 == 1 {
                ({
                    when oa.inner {
                        ma: MA {
                            if 2 == 2 {
                                ({
                                    when ma.inner {
                                        ia: IA { ia.value * 3 }
                                        ib: IB { 0 }
                                    }
                                })
                            } else { 0 }
                        }
                        mb: MB { 0 }
                    }
                })
            } else { 0 }
        }
        ob: OB { 0 }
    }
    if result == 21 {0} else {1}
}

main() {
    error := 0
    error += test_nested_when()
    error += test_nested_when_different_types()
    error += test_deep_if_else()
    error += test_nested_blocks()
    error += test_nested_let_in()
    error += test_mixed_deep_nesting()
    error
}
