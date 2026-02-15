#module main

Shape: | Circle { radius: Int }
       | Square { length: Int }
       | MyNone

Color: | Red { intensity: Int }
       | Blue { intensity: Int }

// when as function argument
pick_value(x: Int) -> Int { x }

test_when_as_argument() {
    s := Circle(5)

    result := pick_value(when s {
        c: Circle { c.radius * 2 }
        sq: Square { sq.length }
        n: MyNone { 0 }
    })
    if result == 10 {0} else {1}
}

// when inside a when arm with different tagged union types
test_when_inside_when() {
    s := Circle(3)
    c := Red(10)

    result := when s {
        ci: Circle {
            when c {
                r: Red  { ci.radius + r.intensity }
                b: Blue { ci.radius + b.intensity }
            }
        }
        sq: Square { 0 }
        n: MyNone { 0 }
    }
    if result == 13 {0} else {1}
}

// when inside a while loop body
test_when_in_while() {
    shapes: CArray[Shape, 3] := void
    shapes.[0] = Circle(1)
    shapes.[1] = Square(2)
    shapes.[2] = Circle(3)

    sum := 0
    i := 0
    ptr: Ptr[Shape] := shapes
    while i < 3, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c: Circle { c.radius }
            sq: Square { sq.length }
            n: MyNone { 0 }
        }
    }
    // 1 + 2 + 3 = 6
    if sum == 6 {0} else {1}
}

// when result used in arithmetic
test_when_arithmetic() {
    a := Circle(10)
    b := Square(5)

    sum := when a {
        c: Circle { c.radius }
        else { 0 }
    } + when b {
        sq: Square { sq.length }
        else { 0 }
    }
    if sum == 15 {0} else {1}
}

// Mutable when (.&) nested inside immutable when
test_mutable_nested_in_immutable() {
    outer := Circle(100)
    inner := Square(7)

    result := when outer {
        c: Circle {
            // Mutable when on inner
            when inner.& {
                sq: Square { sq->length = 42
                             sq->length }
                else { 0 }
            }
        }
        else { 0 }
    }
    // inner.length was mutated to 42
    check := when inner {
        sq: Square { sq.length }
        else { 0 }
    }
    error := 0
    error += if result == 42 {0} else {1}
    error += if check == 42 {0} else {1}
    error
}

// when with multiple nested levels and else
test_when_chained_else() {
    a := MyNone()
    b := Square(8)

    result := when a {
        c: Circle { c.radius }
        else {
            when b {
                sq: Square { sq.length }
                else { -1 }
            }
        }
    }
    if result == 8 {0} else {1}
}

main() {
    error := 0
    error += test_when_as_argument()
    error += test_when_inside_when()
    error += test_when_in_while()
    error += test_when_arithmetic()
    error += test_mutable_nested_in_immutable()
    error += test_when_chained_else()
    error
}
