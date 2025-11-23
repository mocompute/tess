#module main

id (x) { x }

main() {
  val := id("hello")
  res := id(0)
  res
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
