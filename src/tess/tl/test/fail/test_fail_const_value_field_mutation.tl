#module main

Point: { x: Int, y: Int }

main() {
    p: Const[Point] := Point(x = 1, y = 2)
    p.x = 10
    0
}
