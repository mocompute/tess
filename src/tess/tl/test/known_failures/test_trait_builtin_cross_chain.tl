// Known failure: fixed-width and standalone integer types cannot widen across
// subchains to Int/UInt. CInt8 is in FIXED_SIGNED, Int is in C_SIGNED — different
// subchains. The trait dispatch correctly maps CInt8 to module "Int" and finds
// Int__describe__1, but the actual function call fails because CInt8 cannot
// implicitly convert to Int (CLongLong).
//
// This will be fixed when cross-chain integer widening is implemented.

#module Describe
Describe[T] : {
    describe(x: T) -> CInt
}

#module Int
describe(n) -> CInt { 1 }

#module UInt
describe(n) -> CInt { 2 }

#module main

show[T: Describe](x: T) -> CInt { x.describe() }

main() {
    error := 0

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

    error
}
