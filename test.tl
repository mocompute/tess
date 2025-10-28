_tl_sizeof_ (x : a) -> Int
sizeof(T1: Type) { _tl_sizeof_(T1) }

c_malloc(sz : Int) -> Ptr
c_free(ptr : Ptr) -> Nil


: Point(a) {x : a, y : a }

: Array(a) {
  v:    Ptr(a),
  size: Int,
}


array_new(T : Type, count) {
  p : Ptr(T) = c_malloc(count * sizeof(T))
  Array { v = p, size = count }
}

main() {
  a = array_new(Point(Int), 8)
  0
}
