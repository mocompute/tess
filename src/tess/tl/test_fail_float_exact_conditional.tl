#module main

// Exact match required: if-then-else branches CFloat vs CDouble should fail.

main() {
    a: CFloat := 1.0
    b: CDouble := 2.0
    if 1 == 1 { a } else { b }
    0
}
