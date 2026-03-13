#module main

// Test: UFCS implicit address-of (same-module)
// val.f(args) where f expects Ptr[T] should implicitly wrap val in &

Vec2: { x: Int, y: Int }

// Functions that take Ptr (mutating)
vec2_set_x(self: Ptr[Vec2], new_x: Int) -> Void {
    self->x = new_x
}

vec2_set_y(self: Ptr[Vec2], new_y: Int) -> Void {
    self->y = new_y
}

vec2_reset(self: Ptr[Vec2]) -> Void {
    self->x = 0
    self->y = 0
}

// Function that takes value (non-mutating)
vec2_length_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

// Generic function taking Ptr
swap[T](a: Ptr[T], b: Ptr[T]) -> Void {
    tmp := a.*
    a.* = b.*
    b.* = tmp
}

main() {
    error := 0

    // Test 1: Basic implicit address-of — v.vec2_set_x(10) where vec2_set_x expects Ptr[Vec2]
    v := Vec2(x = 3, y = 4)
    v.vec2_set_x(10)
    error += if v.x == 10 {0} else {1}

    // Test 2: Multiple mutating calls in sequence
    v.vec2_set_x(100)
    v.vec2_set_y(200)
    error += if v.x == 100 {0} else {1}
    error += if v.y == 200 {0} else {1}

    // Test 3: Nullary mutating call
    v.vec2_reset()
    error += if v.x == 0 {0} else {1}
    error += if v.y == 0 {0} else {1}

    // Test 4: Non-Ptr first param unchanged — value-taking function still works
    v2 := Vec2(x = 3, y = 4)
    len := v2.vec2_length_sq()
    error += if len == 25 {0} else {1}

    // Test 5: Mixed explicit and implicit
    v3 := Vec2(x = 1, y = 2)
    vec2_set_x(v3.&, 50)       // explicit
    v3.vec2_set_y(60)           // implicit address-of
    error += if v3.x == 50 {0} else {1}
    error += if v3.y == 60 {0} else {1}

    // Test 6: Ptr receiver with . — should NOT double-wrap
    v4 := Vec2(x = 5, y = 6)
    p  := v4.&
    p.vec2_set_x(99)
    error += if v4.x == 99 {0} else {1}

    error
}
