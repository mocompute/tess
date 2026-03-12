#module Vec

// Vec2 has add but explicitly opts out of Add conformance.
[[no_conform(Add)]]
Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 {
    Vec2(x = a.x + b.x, y = a.y + b.y)
}


#module main

main() {
    a := Vec.Vec2(x = 1, y = 2)
    b := Vec.Vec2(x = 3, y = 4)

    // This should fail: Vec2 has [[no_conform(Add)]], so + operator is blocked
    c := a + b
    result: CInt := c.x
    result
}
