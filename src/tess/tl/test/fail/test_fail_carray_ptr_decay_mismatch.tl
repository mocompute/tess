#module main

// CArray[CInt, N] must NOT decay to Ptr[Float] — element type mismatch.

take_float_ptr(p: Ptr[Float]) -> Float { p.[0] }

main() {
    arr: CArray[CInt, 4] := void
    take_float_ptr(arr)
    0
}
