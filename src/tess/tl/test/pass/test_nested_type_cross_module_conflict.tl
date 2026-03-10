#module A

// Tagged union: registers "Shape" in nested_type_parents
Shape: | Circle { radius: Float }
       | Square { length: Float }

#module B

// B defines "Shape" as a plain struct — NOT a nested type parent.
// But because A's Shape registered "Shape" in the global
// nested_type_parents set, B.Shape.Circle would be misinterpreted
// if someone tried it (even though B.Shape has no Circle child).
Shape: { x: Int, y: Int }

#module main

main() {
    error := 0

    // A.Circle — cross-module unscoped constructor (should work)
    circle := A.Circle(2.0)
    area   := when circle {
        c: Circle { c.radius * c.radius }
        s: Square { s.length * s.length }
    }
    error += if area >= 4.0 {0} else {1}

    // B.Shape — plain struct (should work)
    pt := B.Shape(x = 10, y = 20)
    error += if pt.x == 10 {0} else {1}

    // The false positive: B.Shape.Circle would be mangled as B_Shape_Circle
    // instead of being a type error or field access. But there's no valid
    // Tess code that would trigger this — you only write Type.Child when
    // the type actually has children, and field access is on values not types.

    error
}
