#module main

main() {
    x     := 10
    inner := () { x }
    outer := () { inner() }
    outer()
    0
}
