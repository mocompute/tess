#module main

// Exact match required: CFloat + CDouble should fail (different widths).

main() {
    a: CFloat  := 1.0
    b: CDouble := 2.0
    a + b
    0
}
