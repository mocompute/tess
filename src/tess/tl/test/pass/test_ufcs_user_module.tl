// Test: User-defined module with implicit UFCS resolution

#module Geom

Vec3: { x: Int, y: Int, z: Int }

length_sq(v: Vec3) -> Int {
    v.x * v.x + v.y * v.y + v.z * v.z
}

scale(v: Vec3, s: Int) -> Vec3 {
    Vec3(x = v.x * s, y = v.y * s, z = v.z * s)
}

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

set_x(v: Ptr[Vec3], x: Int) -> Void {
    v->x = x
}

reset(v: Ptr[Vec3]) -> Void {
    v->x = 0
    v->y = 0
    v->z = 0
}

#module main

main() {
    error := 0

    v := Geom.Vec3(x = 1, y = 2, z = 3)
    w := Geom.Vec3(x = 10, y = 20, z = 30)

    // Implicit module resolve — value receiver
    error += if v.length_sq() == 14 {0} else {1} // 1+4+9
    b := v.scale(2)
    error += if b.x == 2 && b.y == 4 && b.z == 6 {0} else {1}

    c := v.add(w)
    error += if c.x == 11 && c.y == 22 && c.z == 33 {0} else {1}

    // Implicit module + implicit & — Ptr receiver
    v.set_x(10)
    error += if v.x == 10 {0} else {1}

    // Nullary Ptr receiver
    v.reset()
    error += if v.x == 0 && v.y == 0 && v.z == 0 {0} else {1}

    // Chaining: value→value→scalar
    r := Geom.Vec3(x = 1, y = 0, z = 0).scale(3).add(Geom.Vec3(x = 0, y = 4, z = 0)).length_sq()
    error += if r == 25 {0} else {1}    // (3,4,0) → 9+16+0

    // Explicit .Geom. still works
    d := v.Geom.scale(5)
    error += if d.x == 0 && d.y == 0 && d.z == 0 {0} else {1}

    error
}
