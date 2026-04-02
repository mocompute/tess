#module main

// Using a string literal without importing String.tl should produce a clear
// error message, not an internal compiler failure.

main() {
    s := "hello"
    0
}
