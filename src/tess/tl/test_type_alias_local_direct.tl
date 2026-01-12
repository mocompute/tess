#module Foo
Point : {x: Int, y: Int}
Pt = Point

#module main
main() {
    // Direct usage: construct using local alias
    p := Foo.Pt(x = 10, y = 20)
    res := p.x + p.y
    if (res == 30) { 0 } else { 1 }
}
