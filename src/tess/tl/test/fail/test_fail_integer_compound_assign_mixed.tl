#module main

// Mixed-type compound assignment should fail: CInt += CShort is an
// exact-match context (operator), so different types are rejected.

main() {
    a: CInt   := 10
    b: CShort := 5
    a += b
    0
}
