#module main
#import <stdlib.tl>

Point[a]: { x: a, y: a }

main() {
    p: Ptr[Point[Int]] := c_malloc(sizeof[Point[Int]]())
    p->x = 0
    p->y = 1

    obj := p.*
    c_free(p)
    result: CInt := obj.x
    result
}
