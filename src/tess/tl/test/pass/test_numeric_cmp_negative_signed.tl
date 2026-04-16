// Signed fixed-width comparison must respect sign (negative < positive),
// guarding against an accidental unsigned-load in the C helper.
#module main

main() -> CInt {
    a: CInt32 := -5
    b: CInt32 := 5
    if a.cmp(b) >= 0 { return 1 }
    if b.cmp(a) <= 0 { return 2 }
    if a.eq(b) { return 3 }

    c: CInt8 := -1
    d: CInt8 := 0
    if c.cmp(d) >= 0 { return 4 }
    0
}
