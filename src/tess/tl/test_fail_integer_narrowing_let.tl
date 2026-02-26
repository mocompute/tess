#module main

// Narrowing: Int assigned to CInt via let-in (without cast annotation) should fail.

main() -> CInt {
    a: Int := 42
    b := a   // b infers as Int
    c: CInt := b  // narrowing: Int -> CInt
    c
}
