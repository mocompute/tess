#module Foo

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

#module main

main() {
    error := 0

    // Cross-module scoped positional
    s1 := Foo.Shape.Circle(2.0)
    r1 := when s1 { c: Circle { c.radius } s: Square { s.length } n: MyNone { 0.0 } }
    error += if r1 >= 2.0 {0} else {1}

    s2 := Foo.Shape.Square(3.0)
    r2 := when s2 { c: Circle { c.radius } s: Square { s.length } n: MyNone { 0.0 } }
    error += if r2 >= 3.0 {0} else {1}

    // Cross-module scoped fieldless with parens
    s3 := Foo.Shape.MyNone()
    r3 := when s3 { c: Circle { 1.0 } s: Square { 1.0 } n: MyNone { 0.0 } }
    error += if r3 == 0.0 {0} else {1}

    error
}
