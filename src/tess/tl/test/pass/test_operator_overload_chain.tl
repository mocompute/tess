#module Mat

// A simple 1x1 "matrix" to test chained operator overloading
Mat1: { val: Int }

add(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val + b.val)      }
sub(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val - b.val)      }
mul(a: Mat1, b: Mat1) -> Mat1 { Mat1(val = a.val * b.val)      }
neg(v: Mat1)          -> Mat1          { Mat1(val = 0 - v.val) }
eq (a: Mat1, b: Mat1) -> Bool { a.val == b.val                 }

cmp(a: Mat1, b: Mat1) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


#module main

main() {
    error := 0

    a := Mat.Mat1(val = 2)
    b := Mat.Mat1(val = 3)
    c := Mat.Mat1(val = 5)

    // Chained addition: a + b + c
    r1 := a + b + c
    error += if r1.val == 10 {0} else {1}

    // Chained subtraction: c - b - a
    r2 := c - b - a
    error += if r2.val == 0 {0} else {1}

    // Mixed operators: a * b + c
    r3 := a * b + c
    error += if r3.val == 11 {0} else {1}

    // Double negation
    r4 := -(-a)
    error += if r4 == a {0} else {1}

    // Compound assignments chained
    acc := Mat.Mat1(val = 0)
    acc += a
    acc += b
    acc += c
    error += if acc.val == 10 {0} else {1}

    // Comparison operators chained with logic
    error += if a < b && b < c {0} else {1}
    error += if c > b && b > a {0} else {1}
    error += if a <= a && a >= a {0} else {1}

    // Equality in conditional
    x := if a == a { Mat.Mat1(val = 100) } else { Mat.Mat1(val = 0) }
    error += if x.val == 100 {0} else {1}

    error
}
