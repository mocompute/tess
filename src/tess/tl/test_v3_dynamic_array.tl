_tl_sizeof_ (x : a)        -> Int

c_malloc    (count : Int)  -> PtrOrNull(a)
c_free      (ptr : Ptr) -> Nil

is_null     (p : PtrOrNull(a)) -> Bool
is_ptr      (p : PtrOrNull(a)) -> Bool
ptr         (p : PtrOrNull(a)) -> Ptr(a)

sizeof      (T) { _tl_sizeof_(T) }

is_null(p) {
  if p == null { true } else { false }
}

// -- end std --



Array(a) : {
  v:    PtrOrNull(a),
  size: Int,
}


Point(a) : { x : a, y : a }
Rect(a)  : { topleft: Point(a), botright: Point(a) }

array_new(T : Type, count) {
  // This type annotation is required for the return type of c_malloc to be concrete. Note that it refers to
  // the type argument T.
  p : PtrOrNull(T) := c_malloc(count * sizeof(T))
  Array { v = p, size = count }
}

array_free(arr) { c_free(arr.v) }


make_points(count) { array_new(Point(Int), count) }
make_rects(count)  { array_new(Rect(Int), count) }

main() {
  points := make_points(8)
  rects := make_rects(8)

  points.v[0] = Point(0, 1)
  points.v[1] = Point(2, 3)

  res := points.v[0].x
  if (points.v[0].x != 0) { res = 1 }
  if (points.v[0].y != 1) { res = 1 }
  if (points.v[1].x != 2) { res = 1 }
  if (points.v[1].y != 3) { res = 1 }

  array_free(points)
  array_free(rects)
  res
}
