#module main

Point: { x: Int, y: Int }

main() {
    cp:  Const        := Point(x = 1, y = 2)
    cp2: Const[Point] := Point(x = 3, y = 4)
    n:   Const        := 7
    sum := cp.x + cp2.x + n
    if sum == 11 { 0 } else { 1 }
}
