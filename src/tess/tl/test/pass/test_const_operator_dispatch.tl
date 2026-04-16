// Const[UserType] on the LHS of a binary operator dispatches to the
// user-defined overload in the pointee's module, mirroring the Ptr fallback
// in find_overload_func.
#module Vec

Vec3: { x: Int, y: Int, z: Int }

eq(a: Vec3, b: Vec3) -> Bool { a.x == b.x && a.y == b.y && a.z == b.z }

cmp(a: Vec3, b: Vec3) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else if a.z < b.z { -1 }
    else if a.z > b.z { 1 }
    else { 0 }
}

// --- Type whose eq is written with Const-receiver parameters. Verifies the
// `!is_ptr(params.v[0])` guard accepts Const receivers as well as bare values.
#module Tag

Tag: { id: Int }

eq(a: Const[Tag], b: Const[Tag]) -> Bool { a.id == b.id }

#module main

main() {
    a: Const := Vec.Vec3(x = 1, y = 2, z = 3)
    b: Const := Vec.Vec3(x = 1, y = 2, z = 3)
    c: Const := Vec.Vec3(x = 9, y = 9, z = 9)
    d        := Vec.Vec3(x = 1, y = 2, z = 3)

    // Const on both sides.
    if !(a == b) { return 1 }
    if a == c { return 2 }

    // Const LHS + value RHS.
    if !(a == d) { return 3 }

    // Derived !=.
    if a != b { return 4 }
    if !(a != c) { return 5 }

    // Ordering operators derived from cmp.
    if !(a < c)  { return 6 }
    if !(a <= b) { return 7 }
    if !(a <= c) { return 8 }
    if !(c > a)  { return 9 }
    if !(c >= a) { return 10 }
    if !(b >= a) { return 11 }
    if a > c     { return 12 }
    if c < a     { return 13 }

    // Comparison inside an if condition (no mutation).
    if a == b {
        if !(a < c) { return 14 }
    } else { return 15 }

    // Const-receiver overload: dispatch must still match through the
    // !is_ptr(params.v[0]) guard.
    t1: Const := Tag.Tag(id = 7)
    t2: Const := Tag.Tag(id = 7)
    t3: Const := Tag.Tag(id = 8)
    if !(t1 == t2) { return 16 }
    if t1 == t3    { return 17 }
    if !(t1 != t3) { return 18 }

    0
}
