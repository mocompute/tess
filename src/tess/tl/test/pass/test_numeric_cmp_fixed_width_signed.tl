// Synthesized Int.cmp is generic over T, so fixed-width signed integers
// (CInt8/16/32/64) dispatch correctly despite sharing module="Int" with the
// c-width subchain (CInt/CLong/CLongLong).
#module main

main() {
    a: CInt64 := 10
    b: CInt64 := 20
    if a.cmp(b) >= 0 { return 1 }
    0
}
