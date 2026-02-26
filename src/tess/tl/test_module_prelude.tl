#module_prelude Foo

Point: { x: Int, y: Int }
make_point(x: Int, y: Int) -> Point

#module Foo

make_point(x: Int, y: Int) -> Point {
    Point(x = x, y = y)
}

add(a: Point, b: Point) -> Point {
    Point(x = a.x + b.x, y = a.y + b.y)
}

#module main

main() {
    a := Foo.make_point(1, 2)
    b := Foo.make_point(3, 4)
    c := Foo.add(a, b)
    result : CInt := c.x + c.y - 10
}
