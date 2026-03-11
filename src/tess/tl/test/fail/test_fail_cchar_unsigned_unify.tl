// CChar should not unify with unsigned integer types
#module main
take_uint(x: CUnsignedInt) -> CUnsignedInt { x }
main() {
    c: CChar := 65
    take_uint(c)
}
