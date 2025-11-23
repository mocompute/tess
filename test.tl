#module main


main() {
  a := 1
  b := a
  c := b - 1
  c
}

// Point(a) : { x: a, y: a }

// c_malloc(size: CSize) -> Ptr(T)

// array_new(T : Type, count) {
//   p : Ptr(T) := c_malloc(count * sizeof(T))
//   p
// }

// main() {
//   arr := array_new(Point(Int), 8)
//   0
// }
