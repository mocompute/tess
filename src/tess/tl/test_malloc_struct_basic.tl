#module main
#import <stdlib.tl>

Point(a): { x: a, y: a }

main() {
    p: Ptr(Point(Int)) := c_malloc(_tl_sizeof_(Point(Int)))
    p->x = 0
    p->y = 1
    res := p->x
    c_free(p)
    res
}
