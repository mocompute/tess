#module Foo

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | None

#module main

main() {
    error := 0

    // make_Shape_* wraps a variant value into a Shape tagged union
    // One function per variant, taking a single positional argument

    // Test 1: Wrap a Circle
    c := Foo.Shape.Circle(radius = 3.0)
    shape1 := Foo.make_Shape_Circle(c)
    r1 := case shape1: Foo.Shape {
        c: Circle { c.radius }
        else { 0.0 }
    }
    error += if r1 >= 3.0 {0} else {1}

    // Test 2: Wrap a Square
    s := Foo.Shape.Square(length = 4.0)
    shape2 := Foo.make_Shape_Square(s)
    r2 := case shape2: Foo.Shape {
        s: Square { s.length }
        else { 0.0 }
    }
    error += if r2 >= 4.0 {0} else {1}

    // Test 3: Wrap None
    n := Foo.Shape.None()
    shape3 := Foo.make_Shape_None(n)
    r3 := case shape3: Foo.Shape {
        n: None { 1.0 }
        else { 0.0 }
    }
    error += if r3 >= 1.0 {0} else {1}

    error
}
