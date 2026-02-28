#module main

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

main() {
    error := 0

    // Same-module scoped positional
    s1 := Shape.Circle(2.0)
    r1 := when s1 { c: Circle { c.radius } s: Square { s.length } n: MyNone { 0.0 } }
    error += if r1 >= 2.0 {0} else {1}

    // Same-module scoped named
    s2 := Shape.Circle(radius = 3.0)
    r2 := when s2 { c: Circle { c.radius } else { 0.0 } }
    error += if r2 >= 3.0 {0} else {1}

    // Same-module unscoped named
    s3 := Circle(radius = 4.0)
    r3 := when s3 { c: Circle { c.radius } else { 0.0 } }
    error += if r3 >= 4.0 {0} else {1}

    // Scoped fieldless with parens
    s4 := Shape.MyNone()
    r4 := when s4 { c: Circle { 1.0 } s: Square { 1.0 } n: MyNone { 0.0 } }
    error += if r4 == 0.0 {0} else {1}

    error
}
