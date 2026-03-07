#module Vec

Vec3 : { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

neg(v: Vec3) -> Vec3 {
    Vec3(x = 0 - v.x, y = 0 - v.y, z = 0 - v.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

sub(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x - b.x, y = a.y - b.y, z = a.z - b.z)
}


#module main

main() {
    error := 0

    a := Vec.Vec3(x = 1, y = 2, z = 3)
    b := Vec.Vec3(x = 4, y = 5, z = 6)

    // binary + dispatches to Vec.add
    c := a + b
    error += if c.x == 5 && c.y == 7 && c.z == 9 {0} else {1}

    // binary - dispatches to Vec.sub
    d := a - b
    error += if d.x == 0 - 3 && d.y == 0 - 3 && d.z == 0 - 3 {0} else {1}

    // unary - dispatches to Vec.neg
    e := -a
    error += if e.x == 0 - 1 && e.y == 0 - 2 && e.z == 0 - 3 {0} else {1}

    // == dispatches to Vec.eq
    error += if a == a {0} else {1}
    error += if a == b {1} else {0}

    // != is derived from eq (negated)
    error += if a != b {0} else {1}
    error += if a != a {1} else {0}

    error
}
