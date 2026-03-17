// Let-else where else block has if/else before diverging return.
// Compiler doesn't recognize if/else with both branches returning as diverging.
#module Shape

Shape: | Circle { r: CInt }
       | Square { s: CInt }

#module main

main() {
    s: Shape.Shape := Shape.Square(s = 4)
    c: Circle      := s else {
        fallback := 1
        if fallback == 1 {
            return 0
        } else {
            return 1
        }
    }
    c.r
}
