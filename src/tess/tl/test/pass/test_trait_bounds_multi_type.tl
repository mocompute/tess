// Test that two types in separate modules can both satisfy Add and Ord/Eq
// via the same trait-bounded generic functions.
// Also exercises auto-collapse: each module names its type T, so Vec2(...)
// and Vec3(...) work as constructors instead of requiring Vec2.T(...) etc.

#module Vec2

T: { x: Int, y: Int }

add(a: T, b: T) -> T    { T(x = a.x + b.x, y = a.y + b.y) }
eq (a: T, b: T) -> Bool { a.x == b.x && a.y == b.y        }

cmp(a: T, b: T) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

#module Vec3

T: { x: Int, y: Int, z: Int }

add(a: T, b: T) -> T    { T(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z) }
eq (a: T, b: T) -> Bool { a.x == b.x && a.y == b.y && a.z == b.z         }

cmp(a: T, b: T) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else if a.z < b.z { -1 }
    else if a.z > b.z { 1 }
    else { 0 }
}

#module main

double[T:    Add](x: T)       -> T    { x + x                     }
smallest[T:  Ord](a: T, b: T) -> T    { if a < b { a } else { b } }
are_equal[T: Eq] (a: T, b: T) -> Bool { a == b                    }

main() {
    error := 0

    // Auto-collapse: Vec2.T and Vec3.T are accessible as Vec2 and Vec3
    v2a := Vec2(x = 1, y = 2)
    v2b := Vec2(x = 3, y = 4)

    v3a := Vec3(x = 1, y = 2, z = 3)
    v3b := Vec3(x = 4, y = 5, z = 6)

    // Add works for both types
    v2sum := double(v2a)
    error += if v2sum.x == 2 && v2sum.y == 4 { 0 } else { 1 }

    v3sum := double(v3a)
    error += if v3sum.x == 2 && v3sum.y == 4 && v3sum.z == 6 { 0 } else { 1 }

    // Ord works for both types
    s2 := smallest(v2a, v2b)
    error += if s2.x == 1 && s2.y == 2 { 0 } else { 1 }

    s3 := smallest(v3a, v3b)
    error += if s3.x == 1 && s3.y == 2 && s3.z == 3 { 0 } else { 1 }

    // Eq works for both types
    error += if are_equal(v2a, v2a) { 0 } else { 1 }
    error += if are_equal(v2a, v2b) { 1 } else { 0 }
    error += if are_equal(v3a, v3a) { 0 } else { 1 }
    error += if are_equal(v3a, v3b) { 1 } else { 0 }

    error
}
