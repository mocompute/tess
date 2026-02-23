#module main

// Cross-family: signed + unsigned arithmetic should be a type error.

main() {
    x := 1u + 2
    x
}
