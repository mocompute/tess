// Verify that a module with ONLY a same-named type still auto-collapses fine.

#module Bar

Bar: { x: Int }

#module main

main() -> CInt {
    b := Bar(x = 42)
    if b.x == 42 { 0 } else { 1 }
}
