#module main

Point(a) : { x: a, y: a }

array_new(T : Type, count) {
  p : Ptr(T) := c_malloc(count * sizeof(T))
  p
}

main() {
  arr := array_new(Int, 8)
  0
}
