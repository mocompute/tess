#module Geom

Vec2: { x: Int, y: Int }

len_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

#module main

main() {
    v := Geom.Vec2(x = 1, y = 2)
    v->len_sq()                         // error: v is not a pointer
    0
}
