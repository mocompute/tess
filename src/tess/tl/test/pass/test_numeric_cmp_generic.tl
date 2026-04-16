// Generic functions that call .cmp/.eq on numeric type parameters must dispatch
// through the synthesized per-family cmp/eq rather than failing UFCS resolution.
#module main

less_via_cmp[T: Ord](a: T, b: T) -> Bool { a.cmp(b) < 0 }
equal_via_eq[T: Eq] (a: T, b: T) -> Bool { a.eq(b)      }

main() {
    if !less_via_cmp(1, 2) { return 1 }
    if !less_via_cmp(1.0, 2.0) { return 2 }
    if !equal_via_eq(7, 7) { return 3 }
    if equal_via_eq(7, 8) { return 4 }
    0
}
