#module main

// Runtime failure test: cross-family bounds check.
// When compiled with --bounds-check, casting a negative Int to UInt
// must abort (negative values cannot be represented as unsigned).

main() {
    a:      Int       := -1
    b:      UInt      := a
    result: CInt := b
    result
}
