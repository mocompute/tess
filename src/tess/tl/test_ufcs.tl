#module main

// Test: Uniform Function Call Syntax (UFCS)
// x.foo(a, b) desugars to foo(x, a, b)

Vec2: { x: Int, y: Int }

vec2_length_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

vec2_add(a: Vec2, b: Vec2) -> Vec2 {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}

vec2_scale(v: Vec2, s: Int) -> Vec2 {
    Vec2(x = v.x * s, y = v.y * s)
}

main() {
    error := 0

    v := Vec2(x = 3, y = 4)

    // Test 1: UFCS basic call — v.vec2_length_sq() becomes vec2_length_sq(v)
    len_sq := v.vec2_length_sq()
    error += if len_sq == 25 {0} else {1}

    // Test 2: UFCS with extra arguments — v.vec2_scale(2) becomes vec2_scale(v, 2)
    v2 := v.vec2_scale(2)
    error += if v2.x == 6 {0} else {1}
    error += if v2.y == 8 {0} else {1}

    // Test 3: UFCS with two struct arguments — v.vec2_add(v2) becomes vec2_add(v, v2)
    v3 := v.vec2_add(v2)
    error += if v3.x == 9 {0} else {1}
    error += if v3.y == 12 {0} else {1}

    // Test 4: Struct field access still works (not broken by UFCS)
    error += if v.x == 3 {0} else {1}
    error += if v.y == 4 {0} else {1}

    // Test 5: UFCS chaining — v.vec2_scale(2).vec2_length_sq()
    chained := v.vec2_scale(3).vec2_length_sq()
    error += if chained == 225 {0} else {1}

    // Test 6: UFCS with -> operator — ptr->vec2_length_sq() becomes vec2_length_sq_ptr(ptr)
    ptr     := v.&
    len_sq2 := ptr->vec2_length_sq_ptr()
    error += if len_sq2 == 25 {0} else {1}

    // Test 7: UFCS with -> and extra arguments
    v4 := ptr->vec2_scale_ptr(10)
    error += if v4.x == 30 {0} else {1}
    error += if v4.y == 40 {0} else {1}

    // Test 8: UFCS on generic struct — Pair[Int, Int].sum()
    p := Pair(a = 3, b = 7)
    error += if p.pair_sum() == 10 {0} else {1}

    // Test 9: UFCS with generic function — v.identity() calls identity[Vec2](v)
    v5 := v.identity()
    error += if v5.x == 3 {0} else {1}
    error += if v5.y == 4 {0} else {1}

    // Test 10: UFCS inside generic function — apply_length_sq[Vec2](v)
    error += if apply_length_sq(v) == 25 {0} else {1}

    // Test 11: UFCS with generic function inside generic function
    v6 := v.apply_identity()
    error += if v6.x == 3 {0} else {1}
    error += if v6.y == 4 {0} else {1}

    error
}

Pair[A, B]: { a: A, b: B }

pair_sum(p: Pair(Int, Int)) -> Int { p.a + p.b } // FIXME: invalid type syntax Pair(Int, Int)

identity[T](x: T) -> T { x }

apply_length_sq[T](x: T) -> Int {
    x.vec2_length_sq()
}

apply_identity[T](x: T) -> T {
    x.identity()
}

vec2_length_sq_ptr(v: Ptr(Vec2)) -> Int {
    v->x * v->x + v->y * v->y
}

vec2_scale_ptr(v: Ptr(Vec2), s: Int) -> Vec2 {
    Vec2(x = v->x * s, y = v->y * s)
}
