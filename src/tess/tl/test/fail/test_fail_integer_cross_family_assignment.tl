#module main

// Cross-family: unsigned value should not be assignable to signed variable.

main() {
    x: Int := 1u
    x
}
