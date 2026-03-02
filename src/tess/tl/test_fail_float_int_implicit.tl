#module main

// Float passed to Int parameter (no annotation) should fail.

take_int(x: Int) -> Int { x }

main() {
    a: Float := 3.14
    take_int(a)
    0
}
