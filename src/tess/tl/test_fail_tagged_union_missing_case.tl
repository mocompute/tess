#module Foo

Shape = | Circle { radius: Float }
        | Square { length: Float }
        | None

#module main

main() {
    s := Foo.Circle(2.0)

    // Error: Missing case for 'None' variant (no else arm)
    case s: Foo.Shape {
        c:  Circle { c.radius }
        sq: Square { sq.length }
    }

    0
}
