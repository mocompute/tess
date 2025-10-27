_tl_sizeof_ (x : a) -> Int
sizeof(T: Type) { _tl_sizeof_(T) }

c_malloc(sz : Int) -> Ptr(a)
c_free(ptr : Ptr(a)) -> Nil


: Point(a) {x : a, y : a }

: Array(a) {
  v:    Ptr(a),
  size: Int,
}


array_new(T, count) {
  p : Ptr(T) = c_malloc(count * sizeof(T))
  Array { v = p, size = count }
}

main() {
  a = array_new(Point(Int), 8)
  0
}
