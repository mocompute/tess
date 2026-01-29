#module main

// Simple struct with two fields
Point: { x: Int, y: Int }

// Struct with three fields
Color: { r: Int, g: Int, b: Int }

// Generic struct
Pair(a, b): { first: a, second: b }

main() {
    error := 0

    // Test positional construction - removed from language
    // p1 := Point(10, 20)
    // error += if p1.x == 10 {0} else {1}
    // error += if p1.y == 20 {0} else {1}

    // Test named construction
    p2 := Point(x = 30, y = 40)
    error += if p2.x == 30 {0} else {1}
    error += if p2.y == 40 {0} else {1}

    // Test named construction with different order
    p3 := Point(y = 60, x = 50)
    error += if p3.x == 50 {0} else {1}
    error += if p3.y == 60 {0} else {1}

    // Test three-field struct with positional - removed from language
    // c1 := Color(255, 128, 0)
    // error += if c1.r == 255 {0} else {1}
    // error += if c1.g == 128 {0} else {1}
    // error += if c1.b == 0 {0} else {1}

    // Test three-field struct with named
    c2 := Color(r = 0, g = 255, b = 128)
    error += if c2.r == 0 {0} else {1}
    error += if c2.g == 255 {0} else {1}
    error += if c2.b == 128 {0} else {1}

    // Test generic struct with positional - removed from language
    // pair1 := Pair(1, 2)
    // error += if pair1.first == 1 {0} else {1}
    // error += if pair1.second == 2 {0} else {1}

    // Test generic struct with named
    pair2 := Pair(first = 3, second = 4)
    error += if pair2.first == 3 {0} else {1}
    error += if pair2.second == 4 {0} else {1}

    error
}
