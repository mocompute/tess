#module Foo

Shape: | Circle { radius: Float }
       | Square { length: Float }

#module main

main() {
    error := 0

    // Variant structs are scoped under the tagged union type.
    // Foo.Shape.Circle constructs a bare Circle struct, NOT a Shape.
    bare_circle := Foo.Shape.Circle(radius = 2.0)

    // The bare struct has the expected field value
    error += if bare_circle.radius >= 2.0 {0} else {1}

    // Use make_Shape_Circle to wrap it into a tagged union
    shape := Foo.make_Shape_Circle(bare_circle)

    // Verify it works in a case expression
    result := when shape {
        c: Circle { c.radius }
        s: Square { s.length }
    }
    error += if result >= 2.0 {0} else {1}

    error
}
