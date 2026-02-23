#module main

// Cross-family: unsigned literal should not unify with signed Int parameter.

take_int(x: Int) { x }

main() {
    take_int(1u)
    0
}
