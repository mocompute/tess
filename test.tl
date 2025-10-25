_tl_sizeof_ (x : a) -> Int
c_malloc(sz : Int) -> Ptr(a)

: Point(a) { x: a y : a }

main() {
  p : Ptr(Point(Int)) = c_malloc(_tl_sizeof_(Point(Int)))
  p->x := 0
  p->y := 1
  p->x
}
