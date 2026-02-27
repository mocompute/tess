#module main

// Narrowing: Int value passed to CInt parameter should fail.

take_cint(x: CInt) -> CInt { x }

main() {
    a: Int := 42
    take_cint(a)
    0
}
