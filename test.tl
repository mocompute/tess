_tl_sizeof_ (x : a) -> Int
c_malloc(sz : Int) -> Ptr(a)
c_free(ptr : Ptr(a)) -> Nil

: Point(a) { x: a y : a }

main() {
//  tmp = Point(0, 0) // FIXME currently require a concrete type constructor application to specialize the type
  p : Ptr(Point(Int)) = c_malloc(_tl_sizeof_(Point(Int)))
  p->x := 0
  p->y := 1
  res = p->x
  c_free(p)
  res
}
