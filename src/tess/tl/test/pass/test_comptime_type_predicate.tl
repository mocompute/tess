#module main

test_basic() {
    error := 0

    val:    Int := 0
    val #:: Int

    error
}


main() {
    error := 0

    error += test_basic()

    error
}
