#module main

// add(x: Int) -> Int
// add(x: Int, y: Int) -> Int
// add(x: Int, y: Int, z: Int) -> Int

add(x) { x }
add(x, y) { x + y }
add(x, y, z) { x + y + z }

main() {
    a := add(1)
    b := add(1, 2)
    c := add(1, 2, 3)

    if a != 1 { return 1 }
    if b != 3 { return 2 }
    if c != 6 { return 3 }
    0
}
