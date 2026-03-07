#module Foo

Shape = | Circle { radius: Float }
        | Square { length: Float }

// A different type that is NOT a variant of Shape
Point: { x: Float, y: Float }

#module main

main() {
    s := Foo.Circle(2.0)

    // Error: Point is not a valid variant of Shape
    when s {
        p: Point { 0.0 }
        else { 1.0 }
    }

    0
}
