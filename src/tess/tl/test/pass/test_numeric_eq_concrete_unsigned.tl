// Synthesized eq works on concrete CUInt64 bindings (not weak-int defaulting).
#module main

main() {
    u: CUInt64 := 100zu
    v: CUInt64 := 100zu
    w: CUInt64 := 101zu
    if !u.eq(v) { return 1 }
    if u.eq(w) { return 2 }

    b1: CUInt8 := 255zu
    b2: CUInt8 := 255zu
    if !b1.eq(b2) { return 3 }
    0
}
