#module Point

T[a]: { x: a, y: a }

add(p1, p2) {
    T(x = p1.x + p2.x, y = p1.y + p2.y)
}

eq(p1, p2) {
    p1.x == p2.x && p1.y == p2.y
}

#module main

main() {
    a := Point(x = 1, y = 2)
    b := Point(x = 3, y = 4)
    c := a + b
    c_printf(c"(%d, %d)\n", c.x, c.y)

    if a != b { c_printf(c"different\n") }

    af := Point(x = 1.2, y = 2.3)
    bf := Point(x = 3.4, y = 4.5)
    cf := af + bf
    c_printf(c"(%f, %f)\n", cf.x, cf.y)
    0
}
