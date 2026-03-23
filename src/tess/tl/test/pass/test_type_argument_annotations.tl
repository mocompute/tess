#module main
#import <cstdlib.tl>

Point[a]: { x: a, y: a                            }
Rect[a]:  { topleft: Point[a], botright: Point[a] }

Array[a]: {
    v:    Ptr[a],
    size: CSize,
}


array_new[T](count: CSize) {
    // This type annotation is required for the return type of c_malloc to be concrete. Note that it refers to
    // the type argument T.
    p: Ptr[T] := c_malloc(count * sizeof[T]())
    Array ( v = p, size = count )
}

main() {
    a := array_new[Point[Int]](8zu)
    b := array_new[Rect[Int]](8zu)
    c_free(a.v)
    c_free(b.v)
    0
}
