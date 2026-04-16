// Int.cmp[T] unifies a and b to a single T, so mixing widths must be rejected.
// Users must cast explicitly.
#module main

main() -> CInt {
    a: CInt32 := 10
    b: CInt64 := 20
    if a.cmp(b) >= 0 { return 1 }
    0
}
