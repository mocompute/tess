#module main
#import <stdlib.tl>

Point(a):  { x: a, y: a                            }
Rect (a):  { topleft: Point(a), botright: Point(a) }

Array(a): {
    v:    Ptr(a),
    size: Int,
}


array_new(T: Type, count) {
    // This type annotation is required for the return type of c_malloc to be concrete. Note that it refers to
    // the type argument T.
    p: Ptr(T) := c_malloc(count * sizeof(T))
    Array ( v = p, size = count )
}

main() {
    a := array_new(Point(Int), 8)
    b := array_new(Rect(Int), 8)
    c_free(a.v)
    c_free(b.v)
    0
}
