#module Foo
Point     (T): {x: T, y: T}
Pt = Point(Int)

#module main

main() {
    p   := Foo.Pt(x = 10, y = 20)
    res := p.x + p.y
    if (res == 30) { 0 } else { 1 }
}
