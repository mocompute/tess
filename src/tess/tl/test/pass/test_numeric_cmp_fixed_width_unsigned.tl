// Generic Int.cmp / UInt.cmp covers fixed-width unsigned types (CUInt8/16/32/64)
// that live in a separate subchain from the c-width unsigned family.
#module main

main() -> CInt {
    a: CUInt32 := 10
    b: CUInt32 := 20
    if a.cmp(b) >= 0 { return 1 }
    0
}
