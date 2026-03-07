#module Lib

// Two tagged unions in same module
Color: | Red | Green | Blue
Shape: | Circle { radius: Float } | Square { length: Float }

// Cross-module function returning tagged union
make_circle(r: Float) -> Shape {
    Circle(r)
}

#module main

main() {
    error := 0

    // Both TUs from same module
    s1 := Lib.Circle(5.0)
    r1 := when s1 { c: Circle { c.radius } s: Square { s.length } }
    error += if r1 >= 5.0 {0} else {1}

    c1 := Lib.Color.Red
    r2 := when c1 { r: Red { 1 } g: Green { 2 } b: Blue { 3 } }
    error += if r2 == 1 {0} else {1}

    // Cross-module function returning tagged union
    s2 := Lib.make_circle(7.0)
    r3 := when s2 { c: Circle { c.radius } s: Square { s.length } }
    error += if r3 >= 7.0 {0} else {1}

    error
}
