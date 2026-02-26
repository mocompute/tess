#module main

// Narrowing: Int value passed to CInt parameter should fail.

take_cint(x: CInt) { x }

main() -> CInt {
    a: Int := 42
    take_cint(a)
}
