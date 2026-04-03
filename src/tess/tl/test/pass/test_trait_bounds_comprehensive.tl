// Combined trait bounds tests:
//   test_trait_bounds
//   test_trait_bounds_builtin
//   test_trait_bounds_inherited
//   test_trait_bounds_multi_inst
//   test_trait_bounds_multi_type
//   test_trait_bound_forward_decl
//   test_trait_bound_ptr_conform

#import <Hash.tl>

// --- Modules for test_trait_bounds ---

#module BoundsVec

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

// --- Modules for test_trait_bounds_inherited ---

#module InheritedNum

Number: { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
sub(a: Number, b: Number) -> Number { Number(val = a.val - b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }

cmp(a: Number, b: Number) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}

// --- Modules for test_trait_bounds_multi_inst ---

#module MultiInstVec

Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
eq (a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y           }

#module MultiInstNum

Number: { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }

// --- Modules for test_trait_bounds_multi_type ---

#module MultiVec2

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

#module MultiVec3

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

// --- Module for test_trait_bound_ptr_conform ---

#module Baz

Baz: { v: Int }

eq(a: Ptr[Const[Baz]], b: Ptr[Const[Baz]]) -> Bool {
    a->v == b->v
}


#module main

// === test_trait_bounds ===

bounds_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

bounds_is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

bounds_double[T: Add](x: T) -> T {
    x + x
}

test_trait_bounds() {
    error := 0

    a := BoundsVec.Vec3(x = 1, y = 2, z = 3)
    b := BoundsVec.Vec3(x = 1, y = 2, z = 3)
    c := BoundsVec.Vec3(x = 4, y = 5, z = 6)

    // Call bounded generic function — Vec3 satisfies Eq
    error += if bounds_are_equal(a, b) { 0 } else { 1 }
    error += if bounds_are_equal(a, c) { 1 } else { 0 }

    // Call bounded generic function — Vec3 satisfies Ord (has both eq and cmp)
    error += if bounds_is_less(a, c) { 0 } else { 1 }
    error += if bounds_is_less(c, a) { 1 } else { 0 }

    // Call bounded generic function — Vec3 satisfies Add
    d := bounds_double(a)
    error += if d.x == 2 && d.y == 4 && d.z == 6 { 0 } else { 1 }

    error
}

// === test_trait_bounds_builtin ===

builtin_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

builtin_is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

builtin_double[T: Add](x: T) -> T {
    x + x
}

test_trait_bounds_builtin() {
    error := 0

    // Int satisfies Eq
    error += if builtin_are_equal(1, 1) { 0 } else { 1 }
    error += if builtin_are_equal(1, 2) { 1 } else { 0 }

    // Int satisfies Ord
    error += if builtin_is_less(1, 2) { 0 } else { 1 }
    error += if builtin_is_less(2, 1) { 1 } else { 0 }

    // Int satisfies Add
    error += if builtin_double(5) == 10 { 0 } else { 1 }

    // Float satisfies Eq, Ord, Add
    error += if builtin_are_equal(1.0, 1.0) { 0 } else { 1 }
    error += if builtin_is_less(1.0, 2.0) { 0 } else { 1 }
    error += if builtin_double(2.5) == 5.0 { 0 } else { 1 }

    // Bool satisfies Eq
    error += if builtin_are_equal(1 == 1, 2 == 2) { 0 } else { 1 }

    error
}

// === test_trait_bounds_inherited ===

// User-defined combined trait requiring both Add and Eq
InheritedAddEq[T]: Add[T], Eq[T] { }

// Function using the combined trait
inherited_add_if_different[T: InheritedAddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

// Function requiring Ord (which inherits Eq, so both eq and cmp needed)
inherited_clamp[T: Ord](val: T, lo: T, hi: T) -> T {
    if val < lo { lo }
    else if val > hi { hi }
    else { val }
}

// Multiple bounded type params
inherited_pick_lesser[T: Ord, U: Eq](a: T, b: T, x: U, y: U) -> T {
    // Use T's Ord and U's Eq independently
    if x == y {
        if a < b { a } else { b }
    } else {
        if a > b { a } else { b }
    }
}

test_trait_bounds_inherited() {
    error := 0

    a    := InheritedNum.Number(val = 3)
    b    := InheritedNum.Number(val = 7)
    same := InheritedNum.Number(val = 3)

    // add_if_different: different values -> adds them
    r1 := inherited_add_if_different(a, b)
    error += if r1.val == 10 {0} else {1}

    // add_if_different: same values -> returns first
    r2 := inherited_add_if_different(a, same)
    error += if r2.val == 3 {0} else {1}

    // clamp: value in range
    r3 := inherited_clamp(InheritedNum.Number(val = 5), a, b)
    error += if r3.val == 5 {0} else {1}

    // clamp: value below range
    r4 := inherited_clamp(InheritedNum.Number(val = 1), a, b)
    error += if r4.val == 3 {0} else {1}

    // clamp: value above range
    r5 := inherited_clamp(InheritedNum.Number(val = 10), a, b)
    error += if r5.val == 7 {0} else {1}

    // pick_lesser with matching second pair (picks lesser of first pair)
    r6 := inherited_pick_lesser(b, a, same, a)
    error += if r6.val == 3 {0} else {1}

    // pick_lesser with non-matching second pair (picks greater of first pair)
    r7 := inherited_pick_lesser(a, b, a, b)
    error += if r7.val == 7 {0} else {1}

    error
}

// === test_trait_bounds_multi_inst ===

multi_inst_double[T: Add](x: T) -> T {
    x + x
}

multi_inst_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

test_trait_bounds_multi_inst() {
    error := 0

    v := MultiInstVec.Vec2(x = 3, y = 4)
    n := MultiInstNum.Number(val = 5)

    // double instantiated with Vec2
    v2 := multi_inst_double(v)
    error += if v2.x == 6 && v2.y == 8 {0} else {1}

    // double instantiated with Number
    n2 := multi_inst_double(n)
    error += if n2.val == 10 {0} else {1}

    // are_equal instantiated with Vec2
    error += if multi_inst_are_equal(v, v) {0} else {1}
    error += if multi_inst_are_equal(v, v2) {1} else {0}

    // are_equal instantiated with Number
    error += if multi_inst_are_equal(n, n) {0} else {1}
    error += if multi_inst_are_equal(n, n2) {1} else {0}

    error
}

// === test_trait_bounds_multi_type ===

multi_type_double[T: Add]  (x: T)       -> T    { x + x                     }
multi_type_smallest[T: Ord](a: T, b: T) -> T    { if a < b { a } else { b } }
multi_type_are_equal[T: Eq](a: T, b: T) -> Bool { a == b                    }

test_trait_bounds_multi_type() {
    error := 0

    // Auto-collapse: MultiVec2.T and MultiVec3.T are accessible as MultiVec2 and MultiVec3
    v2a := MultiVec2(x = 1, y = 2)
    v2b := MultiVec2(x = 3, y = 4)

    v3a := MultiVec3(x = 1, y = 2, z = 3)
    v3b := MultiVec3(x = 4, y = 5, z = 6)

    // Add works for both types
    v2sum := multi_type_double(v2a)
    error += if v2sum.x == 2 && v2sum.y == 4 { 0 } else { 1 }

    v3sum := multi_type_double(v3a)
    error += if v3sum.x == 2 && v3sum.y == 4 && v3sum.z == 6 { 0 } else { 1 }

    // Ord works for both types
    s2 := multi_type_smallest(v2a, v2b)
    error += if s2.x == 1 && s2.y == 2 { 0 } else { 1 }

    s3 := multi_type_smallest(v3a, v3b)
    error += if s3.x == 1 && s3.y == 2 && s3.z == 3 { 0 } else { 1 }

    // Eq works for both types
    error += if multi_type_are_equal(v2a, v2a) { 0 } else { 1 }
    error += if multi_type_are_equal(v2a, v2b) { 1 } else { 0 }
    error += if multi_type_are_equal(v3a, v3a) { 0 } else { 1 }
    error += if multi_type_are_equal(v3a, v3b) { 1 } else { 0 }

    error
}

// === test_trait_bound_forward_decl ===

forward_foo[T: Hash](x: T) -> CSize
forward_foo[T: Hash](x: T) -> CSize { x.hash() }

test_trait_bound_forward_decl() {
    h := forward_foo(42)
    if h == 0zu { 1 } else { 0 }
}

// === test_trait_bound_ptr_conform ===

ptr_conform_are_equal[T: Eq](a: T, b: T) -> Bool { a == b }

test_trait_bound_ptr_conform() {
    b1 := Baz(v = 42)
    b2 := Baz(v = 42)
    b3 := Baz(v = 99)

    r := 0

    if !(ptr_conform_are_equal(b1, b2)) { r = r + 1 }
    if ptr_conform_are_equal(b1, b3) { r = r + 1 }

    r
}

// === main ===

main() {
    error := 0
    error += test_trait_bounds()
    error += test_trait_bounds_builtin()
    error += test_trait_bounds_inherited()
    error += test_trait_bounds_multi_inst()
    error += test_trait_bounds_multi_type()
    error += test_trait_bound_forward_decl()
    error += test_trait_bound_ptr_conform()
    error
}
