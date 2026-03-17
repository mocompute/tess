#module main

Vec2: { x: Int, y: Int }

vec2_length_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

main() {
    v   := Vec2(x = 3, y = 4)
    ptr := v.&
    ptr->vec2_length_sq()               // error: -> does not support UFCS
    0
}
