// Test UFCS dispatch on auto-collapsed generic types.
// The auto-collapsed alias c"Point" must unify with c"Point__T" in
// UFCS-rewritten function calls.

#module Point

T[a]: { x: a, y: a }

scale(p, factor) {
    T(x = p.x * factor, y = p.y * factor)
}

dot(p1, p2) {
    p1.x * p2.x + p1.y * p2.y
}

#module main

main() {
    error := 0

    a := Point(x = 3, y = 4)

    // Implicit module resolve: a.scale(2) rewrites to Point.scale(a, 2)
    b := a.scale(2)
    error += if b.x == 6 && b.y == 8 { 0 } else { 1 }

    // Implicit module chaining
    c := a.scale(2).scale(3)
    error += if c.x == 18 && c.y == 24 { 0 } else { 1 }

    // Implicit module returning a scalar
    d := a.dot(a)
    error += if d == 25 { 0 } else { 1 }

    // Explicit .Point. still works
    e := a.Point.scale(2)
    error += if e.x == 6 && e.y == 8 { 0 } else { 1 }

    // Float variant with implicit module
    af := Point(x = 1.5, y = 2.5)
    bf := af.scale(2.0)
    error += if bf.x == 3.0 && bf.y == 5.0 { 0 } else { 1 }

    error
}
