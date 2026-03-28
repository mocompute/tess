#module main
#import <cstdlib.tl>
#import <Array.tl>

Point[a]: { x: a, y: a, next: Ptr[Point[a]] }

make_points(count: CSize) { Array.with_capacity[Point[CInt]](count) }

main() {
    points := make_points(8zu)

    Array.push(points.&, Point(x = 0, y = 1, next = null))
    Array.push(points.&, Point(x = 2, y = 3, next = points.data.[0].&))

    res := points.data.[0].x
    if (points.data.[1].next->x != 0) { res = 1 }

    Array.free(points.&)
    res
}
