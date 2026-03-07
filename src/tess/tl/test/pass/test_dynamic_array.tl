#module main
#import <stdlib.tl>
#import <Array.tl>

Point[a]: { x: a, y: a                            }
Rect[a]:  { topleft: Point[a], botright: Point[a] }

make_points(count: UInt) { Array.with_capacity[Point[CInt]](count) }
make_rects ()            { Array.empty[Rect[CInt]]()               }

main() {
    points := make_points(8u)
    rects  := make_rects()

    Array.push(points.&, Point(x = 0, y = 1))
    Array.push(points.&, Point(x = 2, y = 3))
    Array.push(rects.&, Rect(topleft = points.v.[0], botright = points.v.[1]))

    res := points.v.[0].x
    if (points.v.[0].x != 0) { res = 1 }
    if (points.v.[0].y != 1) { res = 1 }
    if (points.v.[1].x != 2) { res = 1 }
    if (points.v.[1].y != 3) { res = 1 }

    Array.free(points.&)
    Array.free(rects.&)
    res
}
