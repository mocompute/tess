// Verify that a module with ONLY a T type still auto-collapses fine.

#module Baz

T: { y: Int }

#module main

main() -> CInt {
    b := Baz(y = 7)
    if b.y == 7 { 0 } else { 1 }
}
