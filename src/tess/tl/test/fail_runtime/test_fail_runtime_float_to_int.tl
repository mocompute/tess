#module main

// Runtime failure test: float to integer conversion overflow.
// When compiled with --bounds-check, converting a float value that
// exceeds the target integer's range must abort.
// CInt max is 2147483647, so 9999999999.0 overflows.

main() {
    f: Float := 9999999999.0
    i: CInt  := f
    i
}
