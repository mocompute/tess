#module main

// CSize (standalone subchain 5) passed to CUnsignedInt (subchain 2) should
// fail in directed mode — cross-subchain implicit conversion is not allowed.

take_uint(x: CUnsignedInt) -> CUnsignedInt { x }

main() {
    a: CSize := 10zu
    take_uint(a)
    0
}
