#module main
#import <cstring.tl>

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

    // Test 3: sizeof still correct when CArray is also passed to c_memset (with import)
    c_memset(arr, 0, sizeof(arr))
    arr.[0] = 42
    if arr.[0] != 42 {
        c_printf(c"FAIL 3: c_memset + assign failed\n")
        error += 1
    }

    error
}
