// Bool.eq dispatches through the synthesized Bool__eq__2.
#module main

main() -> CInt {
    a := 1 == 1
    b := 2 == 2
    if !a.eq(b) { return 1 }
    0
}
