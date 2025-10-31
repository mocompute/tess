_tl_sizeof_ (x : a) -> Int

// Here T may be a type literal (Type) or a concrete value
sizeof(T) { _tl_sizeof_(T) }

Point(a) : {x : a, y : a }

main() {
  x = 42
  if (sizeof(Int) + sizeof(x) == sizeof(Point(Int))) { 0 } else { 1 }
}
