#module main

test_basic() {
    error := 0

    val:    Int := 0
    val #:: Float

    error
}


main() {
    error := 0

    error += test_basic()

    error
}
