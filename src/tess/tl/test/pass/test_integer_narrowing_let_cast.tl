#module main

// Narrowing via let-in cast annotation (the explicit cast syntax).
// Int -> CInt via type annotation should compile and produce correct value.

main() {
    a: Int  := 42
    b       := a                        // b infers as Int
    c: CInt := b                        // narrowing: Int -> CInt (explicit cast via annotation)
    error   := 0
    error += if c == 42 {0} else {1}
    error
}
