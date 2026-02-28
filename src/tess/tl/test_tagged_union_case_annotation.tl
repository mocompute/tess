#module Shapes

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

#module main

main() {
    error := 0

    // Basic case with type annotation
    s1 := Shapes.Circle(2.0)
    r1 := case s1: Shape {
        c: Circle { c.radius }
        s: Square { s.length }
        n: MyNone { 0.0 }
    }
    error += if r1 >= 2.0 {0} else {1}

    // case with else arm
    s2 := Shapes.Square(3.0)
    r2 := case s2: Shape {
        c: Circle { c.radius }
        else { -1.0 }
    }
    error += if r2 == -1.0 {0} else {1}

    error
}
