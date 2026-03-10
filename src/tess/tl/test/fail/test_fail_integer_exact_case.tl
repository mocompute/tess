#module main

// Exact match required: case arms CInt vs CShort should fail.

main() {
    a: CInt   := 10
    b: CShort := 5
    x         := 1
    case x {
        1 { a }
        2 { b }
    }
    0
}
