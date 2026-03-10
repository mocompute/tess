#module main

// Exact match required: CInt + CShort should fail (different widths in same chain).

main() {
    a: CInt   := 10
    b: CShort := 5
    a + b
    0
}
