#module main

// Exact match required: if-then-else branches CInt vs CShort should fail.

main() -> CInt {
    a: CInt := 10
    b: CShort := 5
    if 1 == 1 { a } else { b }
}
