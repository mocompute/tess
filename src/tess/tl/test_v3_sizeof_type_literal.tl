_tl_sizeof_ (x : a) -> Int
sizeof(T : Type) { _tl_sizeof_(T) }

: Point(a) {x : a, y : a }

main() {
  if (sizeof(Int) + sizeof(Int) == sizeof(Point(Int))) { 0 } else { 1 }
}
