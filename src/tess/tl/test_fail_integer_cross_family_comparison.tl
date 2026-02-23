#module main

// Cross-family: comparing signed and unsigned should be a type error.

main() {
    if 1u == 1 {0} else {1}
}
