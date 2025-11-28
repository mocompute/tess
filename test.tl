#module main

T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

map(f: (a) -> b, arr: T(a)) -> T(b)
