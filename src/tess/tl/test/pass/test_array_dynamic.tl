#module main
#import <cstdlib.tl>
#import <Array.tl>

Point[a]: { x: a, y: a                            }
Rect[a]:  { topleft: Point[a], botright: Point[a] }

make_points(count: CSize) { Array.with_capacity[Point[CInt]](count) }
make_rects ()             { Array.empty[Rect[CInt]]()               }

main() {
    points := make_points(8zu)
    rects  := make_rects()

    Array.push(points.&, Point(x = 0, y = 1))
    Array.push(points.&, Point(x = 2, y = 3))
    Array.push(rects.&, Rect(topleft = points.data.[0], botright = points.data.[1]))

    res := points.data.[0].x
    if (points.data.[0].x != 0) { res = 1 }
    if (points.data.[0].y != 1) { res = 1 }
    if (points.data.[1].x != 2) { res = 1 }
    if (points.data.[1].y != 3) { res = 1 }

    Array.free(points.&)
    Array.free(rects.&)
    res
}
