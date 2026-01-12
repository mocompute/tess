#import "builtin.tl"

#module Foo
Point : {x: Int, y: Int}
Pt = Point

#module main
main() {
    p : Foo.Pt := Foo.Point(x = 10, y = 20)
    res := p.x + p.y
    if (res == 30) { 0 } else { 1 }
}
