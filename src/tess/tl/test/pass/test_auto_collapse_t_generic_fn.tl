#module Point

T[a]: { x: a, y: a }

scale(p, factor) {
  T(x = p.x * factor, y = p.y * factor)
}

map(p, f) {
  T(x = f(p.x), y = f(p.y))
}

#module main

main() {
  error := 0

  // Direct call through auto-collapsed alias
  a := Point(x = 3, y = 4)
  b := Point.scale(a, 2)
  error += if b.x == 6 && b.y == 8 { 0 } else { 1 }

  // Assign result back to same type
  c := Point.scale(b, 3)
  error += if c.x == 18 && c.y == 24 { 0 } else { 1 }

  // map: returns a different specialization (Int -> Int via lambda)
  d := Point.map(a, (x) { x + 10 })
  error += if d.x == 13 && d.y == 14 { 0 } else { 1 }

  // Float variant
  af := Point(x = 1.5, y = 2.5)
  bf := Point.scale(af, 2.0)
  error += if bf.x == 3.0 && bf.y == 5.0 { 0 } else { 1 }

  error
}
