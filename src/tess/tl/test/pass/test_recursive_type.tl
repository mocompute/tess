#module main
#import <stdlib.tl>
#import <Array.tl>

Point: { x: Int, y: Int, next: Ptr[Point] }

make_points(count: UInt) { Array.with_capacity[Point](count) }

main() {
    points := make_points(8u)

    Array.push(points.&, Point(0, 1, null))
    Array.push(points.&, Point(2, 3, points.v.[0].&))

    res := points.v.[0].x
    if (points.v.[0].x != 0) { res       = 1 }
    if (points.v.[0].y != 1) { res       = 1 }
    if (points.v.[1].x != 2) { res       = 1 }
    if (points.v.[1].y != 3) { res       = 1 }
    if (points.v.[1].next->x != 0) { res = 1 }

    Array.free(points.&)
    result: CInt := res
    result
}
