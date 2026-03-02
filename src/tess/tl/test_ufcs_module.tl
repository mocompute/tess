#module veclib

Vec2: { x: Int, y: Int }

length_sq(v: Vec2) -> Int {
    v.x * v.x + v.y * v.y
}

scale(v: Vec2, s: Int) -> Vec2 {
    Vec2(x = v.x * s, y = v.y * s)
}

#module main

main() {
    error := 0

    v := veclib.Vec2(x = 3, y = 4)

    // Test 1: cross-module UFCS — v.veclib.length_sq() becomes veclib.length_sq(v)
    len_sq := v.veclib.length_sq()
    error += if len_sq == 25 {0} else {1}

    // Test 2: cross-module UFCS with extra argument
    v2 := v.veclib.scale(2)
    error += if v2.x == 6 {0} else {1}
    error += if v2.y == 8 {0} else {1}

    // Test 3: chaining cross-module UFCS
    chained := v.veclib.scale(3).veclib.length_sq()
    error += if chained == 225 {0} else {1}

    error
}
