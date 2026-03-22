// Cross-chain integer widening for trait dispatch.
// Fixed-width and standalone integer types widen across subchains to Int/UInt
// (e.g. CInt8 in FIXED_SIGNED → Int in C_SIGNED).

#module Describe

Describe[T]: {
    describe(x: T) -> CInt
}

#module Int
describe(n) -> CInt { 1 }

#module UInt
describe(n) -> CInt { 2 }

#module main

show[T: Describe](x: T)      -> CInt { x.describe() }
id_ptr[T]        (p: Ptr[T]) -> T    { p.*          }

main() {
    error := 0

    // --- By-value trait dispatch (cross-chain widening) ---

    // Fixed-width signed (FIXED_SIGNED → Int: cross-chain)
    a: CInt8 := 1
    error += if show(a) == 1 { 0 } else { 1 }

    b: CInt16 := 2
    error += if show(b) == 1 { 0 } else { 1 }

    c: CInt32 := 3
    error += if show(c) == 1 { 0 } else { 1 }

    d: CInt64 := 4
    error += if show(d) == 1 { 0 } else { 1 }

    // Standalone signed (→ Int: cross-chain via family fallback)
    e: CPtrDiff := 5
    error += if show(e) == 1 { 0 } else { 1 }

    f: CChar := 65
    error += if show(f) == 1 { 0 } else { 1 }

    // Fixed-width unsigned (FIXED_UNSIGNED → UInt: cross-chain)
    g: CUInt8 := 7
    error += if show(g) == 2 { 0 } else { 1 }

    h: CUInt16 := 8
    error += if show(h) == 2 { 0 } else { 1 }

    i: CUInt32 := 9
    error += if show(i) == 2 { 0 } else { 1 }

    j: CUInt64 := 10
    error += if show(j) == 2 { 0 } else { 1 }

    // Standalone unsigned (→ UInt: cross-chain via family fallback)
    k: CSize := 11
    error += if show(k) == 2 { 0 } else { 1 }

    // --- Direct UFCS .describe() on cross-chain types (no generic wrapper) ---

    r: CInt32 := 42
    error += if r.describe() == 1 { 0 } else { 1 }

    s: CUInt8 := 7
    error += if s.describe() == 2 { 0 } else { 1 }

    t: CPtrDiff := 99
    error += if t.describe() == 1 { 0 } else { 1 }

    u: CSize := 88
    error += if u.describe() == 2 { 0 } else { 1 }

    // --- Pointer dereference then by-value dispatch ---
    // Ensures widening works on the dereferenced value, not on the pointer itself

    v:  CInt16      := 10
    vp: Ptr[CInt16] := v.&
    error += if show(vp.*) == 1 { 0 } else { 1 }

    w:  CUInt32      := 20
    wp: Ptr[CUInt32] := w.&
    error += if show(wp.*) == 2 { 0 } else { 1 }

    x:  CChar      := 65
    xp: Ptr[CChar] := x.&
    error += if show(xp.*) == 1 { 0 } else { 1 }

    y:  CSize      := 42
    yp: Ptr[CSize] := y.&
    error += if show(yp.*) == 2 { 0 } else { 1 }

    // --- Address-of on cross-chain types passed to explicit Ptr parameter ---
    // Ensures pointer-to-cross-chain-integer doesn't trigger widening

    z: CInt8 := 77
    error += if show(id_ptr(z.&)) == 1 { 0 } else { 1 }

    aa: CUInt16 := 88
    error += if show(id_ptr(aa.&)) == 2 { 0 } else { 1 }

    error
}
