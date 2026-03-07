#module Vec

Vec3: { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

sub(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x - b.x, y = a.y - b.y, z = a.z - b.z)
}

mul(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x * b.x, y = a.y * b.y, z = a.z * b.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

cmp(a: Vec3, b: Vec3) -> CInt {
    // Compare lexicographically: x, then y, then z
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else if a.z < b.z { -1 }
    else if a.z > b.z { 1 }
    else { 0 }
}

#module main

main() {
    error := 0

    a := Vec.Vec3(x = 1, y = 2, z = 3)
    b := Vec.Vec3(x = 4, y = 5, z = 6)

    // Compound assignment += dispatches to Vec.add
    c := a
    c += b
    error += if c.x == 5 && c.y == 7 && c.z == 9 {0} else {1}

    // Compound assignment -= dispatches to Vec.sub
    d := b
    d -= a
    error += if d.x == 3 && d.y == 3 && d.z == 3 {0} else {1}

    // Compound assignment *= dispatches to Vec.mul
    e := Vec.Vec3(x = 2, y = 3, z = 4)
    f := Vec.Vec3(x = 5, y = 6, z = 7)
    e *= f
    error += if e.x == 10 && e.y == 18 && e.z == 28 {0} else {1}

    // Derived comparison < via cmp
    lo := Vec.Vec3(x = 1, y = 2, z = 3)
    hi := Vec.Vec3(x = 2, y = 0, z = 0)
    error += if lo < hi {0} else {1}
    error += if hi < lo {1} else {0}

    // Derived comparison <= via cmp
    error += if lo <= hi {0} else {1}
    error += if lo <= lo {0} else {1}
    error += if hi <= lo {1} else {0}

    // Derived comparison > via cmp
    error += if hi > lo {0} else {1}
    error += if lo > hi {1} else {0}

    // Derived comparison >= via cmp
    error += if hi >= lo {0} else {1}
    error += if lo >= lo {0} else {1}
    error += if lo >= hi {1} else {0}

    error
}
