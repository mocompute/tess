#module main
#import <stdlib.tl>

T(a) : {
  v:            Ptr(a),
  size:         Int,
  capacity:     Int,
}

map(f: (a) -> b, arr: T(a)) -> T(b)

map(f: (a) -> b, arr: T(a)) {
  out_p : Ptr(b) := c_malloc(sizeof(b) * arr.size)
  out := T(v = out_p, size = arr.size, capacity = arr.size)

  i := 0
  while i < arr.size {
    mapped := f(arr.v[i])
    out.v[i] = mapped
    i = i + 1
  }
  out
}

main() {

  vals := T(v = c_malloc(sizeof(Int) * 4), size = 0, capacity = 4)

  vals.v[0] = 0
  vals.v[1] = 1
  vals.v[2] = 2
  vals.v[3] = 3
  vals.size = 4

  res := map((x) { x + 1 }, vals)

  // verify that map does not mutate its argument
  sum := 0
  i := 0
  while i < vals.size, i = i + 1 {
    sum = sum + vals.v[i]
  }
  c_assert(sum == 6)

  // verify that map returns an array with mapped values
  sum := 0
  i := 0
  while i < vals.size, i = i + 1 {
    sum = sum + res.v[i]
  }
  c_assert(sum == 10)

  sum - 10
}
