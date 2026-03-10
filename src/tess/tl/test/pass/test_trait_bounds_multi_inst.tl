#module Vec

Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
eq (a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y           }


#module Num

Number: { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }


#module main

// Same generic function instantiated with two different types
double[T: Add](x: T) -> T {
    x + x
}

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    error := 0

    v := Vec.Vec2(x = 3, y = 4)
    n := Num.Number(val = 5)

    // double instantiated with Vec2
    v2 := double(v)
    error += if v2.x == 6 && v2.y == 8 {0} else {1}

    // double instantiated with Number
    n2 := double(n)
    error += if n2.val == 10 {0} else {1}

    // are_equal instantiated with Vec2
    error += if are_equal(v, v) {0} else {1}
    error += if are_equal(v, v2) {1} else {0}

    // are_equal instantiated with Number
    error += if are_equal(n, n) {0} else {1}
    error += if are_equal(n, n2) {1} else {0}

    error
}
