#module Foo

// Tagged union syntax
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

#module main

main() {
    error := 0

    // Test constructor functions (unscoped, positional args)
    circle := Foo.Circle(2.0)
    square := Foo.Square(1.5)
    none   := Foo.MyNone()

    // Test 1: Exhaustive case expression (all variants covered, no else needed)
    area1 := case circle: Foo.Shape {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        n:  MyNone { 0.0 }
    }
    error += if area1 >= 12.0 {0} else {1}

    // Test 2: Partial coverage with else clause (omits MyNone variant)
    area2 := case square: Foo.Shape {
        c:  Circle { c.radius * c.radius * 3.14159 }
        sq: Square { sq.length * sq.length }
        else { 0.0 }                    // handles MyNone and any other variant
    }
    error += if area2 >= 2.25 {0} else {1}

    // Test 3: Else clause handles multiple omitted variants (only Circle)
    area3 := case none: Foo.Shape {
        c: Circle { c.radius * c.radius * 3.14159 }
        else { -1.0 }                   // handles Square and MyNone
    }
    error += if area3 == -1.0 {0} else {1}

    // Test 4: mutable case: use `.&` suffix on case variable
    area4 := case square.&: Foo.Shape {
        sq: Square { sq->length = 3.0
            sq->length * sq->length }
        else { 0.0 }
    }
    error += if area4 >= 9.0 {0} else {1}

    error
}
