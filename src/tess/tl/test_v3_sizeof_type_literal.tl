_tl_sizeof_ (x : a) -> Int
sizeof(T : Type) { _tl_sizeof_(T) }

: Point(a) {x : a, y : a }

main() {
  if (16 == sizeof(Point(Int))) { 0 } else { 1 }
}
