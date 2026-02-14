#module Foo

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

#module main

main() {
    error := 0

    // make_Shape_* wraps a variant value into a Shape tagged union
    // One function per variant, taking a single positional argument

    // Test 1: Wrap a Circle
    c := Foo.Shape.Circle(radius = 3.0)
    shape1 := Foo.make_Shape_Circle(c)
    r1 := when shape1 {
        c: Circle { c.radius }
        else { 0.0 }
    }
    error += if r1 >= 3.0 {0} else {1}

    // Test 2: Wrap a Square
    s := Foo.Shape.Square(length = 4.0)
    shape2 := Foo.make_Shape_Square(s)
    r2 := when shape2 {
        s: Square { s.length }
        else { 0.0 }
    }
    error += if r2 >= 4.0 {0} else {1}

    // Test 3: Wrap MyNone
    n := Foo.Shape.MyNone()
    shape3 := Foo.make_Shape_MyNone(n)
    r3 := when shape3 {
        n: MyNone { 1.0 }
        else { 0.0 }
    }
    error += if r3 >= 1.0 {0} else {1}

    error
}
