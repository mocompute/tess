// Test: Arrow deref + implicit module resolve

#module Geom

Vec3: { x: Int, y: Int, z: Int }

length_sq(v: Vec3) -> Int {
    v.x * v.x + v.y * v.y + v.z * v.z
}

scale(v: Vec3, s: Int) -> Vec3 {
    Vec3(x = v.x * s, y = v.y * s, z = v.z * s)
}

#module main
#import <Array.tl>

main() {
    error := 0

    // Array via pointer — arrow deref resolves Array module from pointee type
    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)
    arr.push(30)

    ptr := arr.&

    // Arrow deref + implicit module on value-receiver functions
    error += if ptr->get(0zu) == 10 {0} else {1}
    error += if ptr->is_empty() == false {0} else {1}
    error += if ptr->contains(20) == true {0} else {1}
    error += if ptr->front() == 10 {0} else {1}
    error += if ptr->back() == 30 {0} else {1}

    arr.free()

    // User-defined: Ptr[Vec3] → arrow deref resolves Geom module
    v := Geom.Vec3(x = 3, y = 4, z = 0)
    vp := v.&
    error += if vp->length_sq() == 25 {0} else {1}
    s := vp->scale(2)
    error += if s.x == 6 && s.y == 8 && s.z == 0 {0} else {1}

    error
}
