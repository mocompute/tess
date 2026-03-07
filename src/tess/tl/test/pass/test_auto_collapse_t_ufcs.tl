// Test UFCS dispatch on auto-collapsed generic types.
// The auto-collapsed alias "Point" must unify with "Point__T" in
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

  // UFCS: a.Point.scale(2) rewrites to Point.scale(a, 2)
  b := a.Point.scale(2)
  error += if b.x == 6 && b.y == 8 { 0 } else { 1 }

  // UFCS chaining
  c := a.Point.scale(2).Point.scale(3)
  error += if c.x == 18 && c.y == 24 { 0 } else { 1 }

  // UFCS returning a scalar
  d := a.Point.dot(a)
  error += if d == 25 { 0 } else { 1 }

  // Float variant
  af := Point(x = 1.5, y = 2.5)
  bf := af.Point.scale(2.0)
  error += if bf.x == 3.0 && bf.y == 5.0 { 0 } else { 1 }

  error
}
