#module main

// Regression: sizeof must return the full array size for CArray, not the
// decayed pointer size.

main() {
    error := 0

    // Test 1: sizeof(expr) on a CArray binding
    arr: CArray[CInt, 16] := void
    expected              := 16zu * sizeof[CInt]()
    actual                := sizeof(arr)

    if actual != expected {
        c_printf(c"FAIL 1: sizeof(arr) = %zu, expected %zu\n", actual, expected)
        error += 1
    }

    // Test 2: sizeof[CArray[T, N]]() type-argument form
    actual2 := sizeof[CArray[CInt, 16]]()

    if actual2 != expected {
        c_printf(c"FAIL 2: sizeof[CArray[CInt, 16]]() = %zu, expected %zu\n", actual2, expected)
        error += 1
    }

    error
}
