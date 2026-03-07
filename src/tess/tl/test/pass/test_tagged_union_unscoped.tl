#module Foo

// Tagged union definition
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

#module main

main() {
    error := 0

    // Departure 1: Unscoped constructor functions with positional arguments
    // Circle() is a module-level function returning Shape
    circle := Foo.Circle(2.0)
    square := Foo.Square(1.5)
    none   := Foo.MyNone()

    // Test 1: Exhaustive when expression
    area1 := when circle {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        n:  MyNone { 0.0 }
    }
    error += if area1 >= 12.0 {0} else {1}

    // Test 2: Partial coverage with else
    area2 := when square {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        else { 0.0 }
    }
    error += if area2 >= 2.25 {0} else {1}

    // Test 3: Mutable when
    area3 := when square.& {
        sq: Square { sq->length = 3.0
            sq->length * sq->length }
        else { 0.0 }
    }
    error += if area3 >= 9.0 {0} else {1}

    error
}
