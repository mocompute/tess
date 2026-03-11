#module main

// CPtrDiff (standalone subchain 6) passed to CInt (subchain 1) should
// fail in directed mode — cross-subchain implicit conversion is not allowed.

take_int(x: CInt) -> CInt { x }

main() {
    a: CPtrDiff := 10z
    take_int(a)
    0
}
