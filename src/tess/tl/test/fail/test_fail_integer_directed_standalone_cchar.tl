#module main

// CChar (standalone subchain 7) passed to CSignedChar (subchain 1) should
// fail in directed mode — cross-subchain implicit conversion is not allowed.

take_schar(x: CSignedChar) -> CSignedChar { x }

main() {
    a: CChar := 'x'
    take_schar(a)
    0
}
