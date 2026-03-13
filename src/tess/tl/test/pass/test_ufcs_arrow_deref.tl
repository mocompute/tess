#module main

// Test: UFCS -> operator dereferences receiver before passing to function

Vec2: { x: Int, y: Int }

vec2_length_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

vec2_scale(v: Vec2, s: Int) -> Vec2 {
    Vec2(x = v.x * s, y = v.y * s)
}

// A function that takes Ptr[Vec2]
vec2_set_x(v: Ptr[Vec2], new_x: Int) -> Void {
    v->x = new_x
}

main() {
    error := 0

    v := Vec2(x = 3, y = 4)
    ptr := v.&

    // Test 1: ptr->f() where f takes value T — dereferences and passes value
    len_sq := ptr->vec2_length_sq()
    error += if len_sq == 25 {0} else {1}

    // Test 2: ptr->f(args) where f takes value T with extra args
    v2 := ptr->vec2_scale(10)
    error += if v2.x == 30 {0} else {1}
    error += if v2.y == 40 {0} else {1}

    // Test 3: ptr->f() where f takes Ptr[T] — pass as-is (no double-deref)
    ptr->vec2_set_x(99)
    error += if v.x == 99 {0} else {1}

    // Test 4: . vs -> distinction — same value-taking function
    v3 := Vec2(x = 1, y = 2)
    len_dot   := v3.vec2_length_sq()      // val.f() → f(val)
    p3        := v3.&
    len_arrow := p3->vec2_length_sq()     // ptr->f() → f(ptr.*)
    error += if len_dot == len_arrow {0} else {1}

    error
}
