#module Vec

Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
sub(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x - b.x, y = a.y - b.y) }
mul(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x * b.x, y = a.y * b.y) }
neg(v: Vec2)          -> Vec2 { Vec2(x = 0 - v.x, y = 0 - v.y)     }
eq (a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y           }


#module Color

RGB: { r: Int, g: Int, b: Int }

add(a: RGB, b: RGB) -> RGB  { RGB(r = a.r + b.r, g = a.g + b.g, b = a.b + b.b) }
eq (a: RGB, b: RGB) -> Bool { a.r == b.r && a.g == b.g && a.b == b.b           }


#module main

main() {
    error := 0

    // Test Vec2 mul operator
    a := Vec.Vec2(x = 3, y = 4)
    b := Vec.Vec2(x = 2, y = 5)
    c := a * b
    error += if c.x == 6 && c.y == 20 {0} else {1}

    // Test chained operations: (a + b) - a == b
    d := a + b - a
    error += if d == b {0} else {1}

    // Test negation then add: -a + a == zero
    zero := Vec.Vec2(x = 0, y = 0)
    e    := -a + a
    error += if e == zero {0} else {1}

    // Test different type (Color) in same program
    red   := Color.RGB(r = 255, g = 0, b = 0)
    green := Color.RGB(r = 0, g = 128, b = 0)
    mixed := red + green
    error += if mixed.r == 255 && mixed.g == 128 && mixed.b == 0 {0} else {1}

    // Test equality on Color
    error += if red == red {0} else {1}
    error += if red != green {0} else {1}

    error
}
