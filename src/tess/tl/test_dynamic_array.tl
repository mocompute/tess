#module main
#import <stdlib.tl>
#import <Array.tl>

Point(a) : { x : a, y : a }
Rect(a)  : { topleft: Point(a), botright: Point(a) }

make_points(count) { Array.with_capacity(Point(Int), count) }
make_rects(count)  { Array.with_capacity(Rect(Int), count) }

main() {
  points := make_points(8)
  rects := make_rects(8)

  Array.push(points.&, Point(0, 1))
  Array.push(points.&, Point(2, 3))

  res := points.v[0].x
  if (points.v[0].x != 0) { res = 1 }
  if (points.v[0].y != 1) { res = 1 }
  if (points.v[1].x != 2) { res = 1 }
  if (points.v[1].y != 3) { res = 1 }

  Array.free(points)
  Array.free(rects)
  res
}
