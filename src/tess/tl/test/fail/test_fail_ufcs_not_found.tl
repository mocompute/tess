#module main

Vec2: { x: Int, y: Int }

main() {
    v := Vec2(x = 1, y = 2)
    v.nonexistent()
}
