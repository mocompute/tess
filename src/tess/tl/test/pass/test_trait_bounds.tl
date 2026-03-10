#module Vec

Vec3: { x: Int, y: Int, z: Int }

add(a: Vec3, b: Vec3) -> Vec3 {
    Vec3(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z)
}

eq(a: Vec3, b: Vec3) -> Bool {
    a.x == b.x && a.y == b.y && a.z == b.z
}

cmp(a: Vec3, b: Vec3) -> CInt {
    dx: CInt := a.x - b.x
    dy: CInt := a.y - b.y
    dz: CInt := a.z - b.z
    if dx != 0 { dx }
    else if dy != 0 { dy }
    else { dz }
}


#module main

// Generic function with a trait bound: T must satisfy Eq
are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

// Generic function with Ord bound (inherits Eq)
is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

// Generic function with Add bound
double[T: Add](x: T) -> T {
    x + x
}

main() {
    error := 0

    a := Vec.Vec3(x = 1, y = 2, z = 3)
    b := Vec.Vec3(x = 1, y = 2, z = 3)
    c := Vec.Vec3(x = 4, y = 5, z = 6)

    // Call bounded generic function — Vec3 satisfies Eq
    error += if are_equal(a, b) { 0 } else { 1 }
    error += if are_equal(a, c) { 1 } else { 0 }

    // Call bounded generic function — Vec3 satisfies Ord (has both eq and cmp)
    error += if is_less(a, c) { 0 } else { 1 }
    error += if is_less(c, a) { 1 } else { 0 }

    // Call bounded generic function — Vec3 satisfies Add
    d := double(a)
    error += if d.x == 2 && d.y == 4 && d.z == 6 { 0 } else { 1 }

    error
}
