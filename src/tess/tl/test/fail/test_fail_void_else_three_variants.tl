#module Tri

Triple: | A | B | C

#module main

// void-else on a three-variant union should fail
test() -> CInt {
    x: Tri.Triple := Tri.A()
    x else err { return 1 }
    0
}

main() { test() }
