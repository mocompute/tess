#module Foo

Shape: | Circle { radius: Float }
       | Square { length: Float }

#module main

main() {
    error := 0

    // Scoped construction with named args returns Shape (not bare struct)
    shape1 := Foo.Shape.Circle(radius = 2.0)

    r1 := when shape1 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r1 >= 2.0 {0} else {1}

    // Unscoped construction with named args also returns Shape
    shape2 := Foo.Circle(radius = 3.0)

    r2 := when shape2 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r2 >= 3.0 {0} else {1}

    // Scoped construction with named args (existing behavior)
    shape3 := Foo.Shape.Square(length = 4.0)

    r3 := when shape3 {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if r3 >= 4.0 {0} else {1}

    error
}
