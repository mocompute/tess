#module main

is_int(x) { x :: Int }

main() {
    error := 0

    // Each application of is_int is specialized based on the type of its argument.
    error := if is_int(1) {0} else {1}
    error := if is_int(0.5) {1} else {0}
    error := if is_int("abc") {1} else {0}

    error
}
