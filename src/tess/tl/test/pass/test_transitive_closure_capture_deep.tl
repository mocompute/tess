#module main

main() {
    x := 10
    y := 20
    inner := () { x + y }
    middle := () { inner() }
    outer := () { middle() }
    result := outer()
    if result == 30 { 0 } else { 1 }
}
