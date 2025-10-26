: Point(a) { x : a,  y : a }
: Rect(a)  { topleft: Point(a), botright: Point(a) }

main() {
  r = Rect { topleft = Point { x = 0, y = 0 }, botright = Point { x = 10, y = 10 } }
  r.topleft.x
}
