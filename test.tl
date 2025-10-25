_tl_sizeof_ (x : a) -> Int
c_malloc(sz : Int) -> Ptr(a)

: Point(a) { x: a y : a }

main() {
  tmp = Point(0, 0)
  p : Ptr(Point(Int)) = c_malloc(_tl_sizeof_(tmp))
  p->x := 1
  p->y := 0
  p
}
