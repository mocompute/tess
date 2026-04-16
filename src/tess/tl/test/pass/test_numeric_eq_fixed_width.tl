// Generic Int.eq / UInt.eq covers fixed-width types that don't canonicalize
// to the c-width subchain's representative.
#module main

main() -> CInt {
    a: CInt32 := 7
    b: CInt32 := 7
    if !a.eq(b) { return 1 }
    0
}
