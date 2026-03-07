#module Foo
Point: {x: Int, y: Int}
Pt = Point

#module main

main() {
    error := 0

    // Via type annotation: construct using original type, annotate with alias
    p1: Foo.Pt := Foo.Point(x = 10, y = 20)
    error += if p1.x + p1.y == 30 {0} else {1}

    // Direct usage: construct using local alias
    p2 := Foo.Pt(x = 10, y = 20)
    error += if p2.x + p2.y == 30 {0} else {1}

    error
}
