// eq() takes (Pt, CInt) instead of (Pt, Pt) — should fail trait satisfaction.
#module Pt

Pt: { x: CInt }

eq(a: Pt, b: CInt) -> Bool { a.x == b }

#module main

are_equal[T: Eq](a: T, b: T) -> Bool { a == b }

main() {
    a := Pt.Pt(x = 1)
    b := Pt.Pt(x = 1)
    if are_equal(a, b) { 0 } else { 1 }
}
