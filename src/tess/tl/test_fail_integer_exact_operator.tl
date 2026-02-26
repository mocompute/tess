#module main

// Exact match required: CInt + CShort should fail (different widths in same chain).

main() -> CInt {
    a: CInt := 10
    b: CShort := 5
    a + b
}
