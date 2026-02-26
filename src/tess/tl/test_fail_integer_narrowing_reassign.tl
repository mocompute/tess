#module main

// Narrowing: Int value assigned to CInt variable via reassignment should fail.

main() -> CInt {
    x: CInt := 0
    a: Int := 42
    x = a
    x
}
