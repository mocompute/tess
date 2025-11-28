#module main
#import <stdlib.tl>

T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

// FIXME: this foward decl should not interfere with type inference
// map(f: (a) -> b, arr: T(a)) -> T(b)

map(f: (a) -> b, arr: T(a)) {
  out_p : Ptr(b) := c_malloc(sizeof(b) * arr.size)
  out := T(v = out_p, size = arr.size, capacity = arr.size)

  i := 0
  while i < arr.size, i = i + 1 {
    mapped := f(arr.v[i])
    out.v[i] = mapped
  }
  out
}

add1(x) { x + 1 }

main() {

  p : Ptr(Int) := c_malloc(sizeof(Int) * 4)
  vals := T(v = p, size = 0, capacity = 4)

  // FIXME: this should work, not needing intermediate value
  // vals := T(v : Ptr(Int) = c_malloc(sizeof(Int) * 4), size = 0, capacity = 4)

  p[0] = 0
  p[1] = 1
  p[2] = 2
  p[3] = 3
  vals.size = 4

  res := map(add1, vals)

  res.v[0] - 1

}
