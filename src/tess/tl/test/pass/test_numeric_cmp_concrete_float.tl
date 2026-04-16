// Synthesized Float.cmp/eq work on concrete CFloat and CDouble bindings
// (not just weak-float literals that default to CDouble).
#module main

main() {
    x: CFloat := 1.5
    y: CFloat := 2.5
    if c_tl_cmp_float(x.&, y.&, sizeof(x)) >= 0 { return 1 }
    if x.cmp(y) >= 0 { return 2 }
    if !x.eq(x) { return 3 }

    d1: CDouble := 3.14
    d2: CDouble := 2.71
    if d1.cmp(d2) <= 0 { return 4 }
    if d1.eq(d2) { return 5 }
    0
}
