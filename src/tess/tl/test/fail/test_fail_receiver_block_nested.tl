// Receiver blocks must be at module top level, not inside function bodies.

#module main

main() {
    s: Int : {
        foo() -> Int { s }
    }
    0
}
