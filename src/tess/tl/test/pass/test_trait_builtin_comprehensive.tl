// Combined trait builtin tests:
//   test_trait_builtin_all_numeric
//   test_trait_builtin_cross_chain
//   test_trait_builtin_family
//   test_trait_builtin_user_trait

#import <String.tl>

// --- Trait and modules for all_numeric and cross_chain (CInt return) ---

#module Classify

Classify[T]: {
    classify(x: T) -> CInt
}

// Signed integers return 1, unsigned return 2, floats return 3, Bool returns 4.
#module Int
classify(n) -> CInt { 1 }

#module UInt
classify(n) -> CInt { 2 }

#module Float
classify(f) -> CInt { 3 }

#module Bool
classify(b) -> CInt { 4 }

// --- Trait and modules for family and user_trait (String return) ---

#module Describe

Describe[T]: {
    describe(x: T) -> String
}

#module Int
describe(n) -> String { String.from_int(n) }

#module Bool
describe(b) -> String { String.from_bool(b) }

#module Float
describe(f) -> String { String.from_float(f) }


#module main

// === test_trait_builtin_all_numeric ===

all_numeric_show[T: Classify](x: T) -> CInt { x.classify() }

test_trait_builtin_all_numeric() {
    error := 0

    // === C-named signed integers (subchain C_SIGNED, all widen to Int) ===

    si1: CSignedChar := 1
    error += if all_numeric_show(si1) == 1 { 0 } else { 1 }

    si2: CShort := 2
    error += if all_numeric_show(si2) == 1 { 0 } else { 1 }

    si3: CInt := 3
    error += if all_numeric_show(si3) == 1 { 0 } else { 1 }

    si4: CLong := 4
    error += if all_numeric_show(si4) == 1 { 0 } else { 1 }

    // CLongLong = Int (canonical)
    si5: Int := 5
    error += if all_numeric_show(si5) == 1 { 0 } else { 1 }

    // === C-named unsigned integers (subchain C_UNSIGNED, all widen to UInt) ===

    ui1: CUnsignedChar := 1
    error += if all_numeric_show(ui1) == 2 { 0 } else { 1 }

    ui2: CUnsignedShort := 2
    error += if all_numeric_show(ui2) == 2 { 0 } else { 1 }

    ui3: CUnsignedInt := 3
    error += if all_numeric_show(ui3) == 2 { 0 } else { 1 }

    ui4: CUnsignedLong := 4
    error += if all_numeric_show(ui4) == 2 { 0 } else { 1 }

    // CUnsignedLongLong = UInt (canonical)
    ui5: UInt := 5
    error += if all_numeric_show(ui5) == 2 { 0 } else { 1 }

    // === Float types (subchain FLOAT, all widen to Float) ===

    f1: CFloat := 1.0
    error += if all_numeric_show(f1) == 3 { 0 } else { 1 }

    // CDouble = Float (canonical)
    f2: Float := 2.0
    error += if all_numeric_show(f2) == 3 { 0 } else { 1 }

    f3: CLongDouble := 3.0
    error += if all_numeric_show(f3) == 3 { 0 } else { 1 }

    // === Bool ===

    error += if all_numeric_show(true) == 4 { 0 } else { 1 }
    error += if all_numeric_show(false) == 4 { 0 } else { 1 }

    error
}

// === test_trait_builtin_cross_chain ===

cross_chain_show[T: Classify](x: T) -> CInt { x.classify() }
cross_chain_id_ptr[T](p: Ptr[T]) -> T       { p.*          }

test_trait_builtin_cross_chain() {
    error := 0

    // --- By-value trait dispatch (cross-chain widening) ---

    // Fixed-width signed (FIXED_SIGNED -> Int: cross-chain)
    a: CInt8 := 1
    error += if cross_chain_show(a) == 1 { 0 } else { 1 }

    b: CInt16 := 2
    error += if cross_chain_show(b) == 1 { 0 } else { 1 }

    c: CInt32 := 3
    error += if cross_chain_show(c) == 1 { 0 } else { 1 }

    d: CInt64 := 4
    error += if cross_chain_show(d) == 1 { 0 } else { 1 }

    // Standalone signed (-> Int: cross-chain via family fallback)
    e: CPtrDiff := 5
    error += if cross_chain_show(e) == 1 { 0 } else { 1 }

    f: CChar := 65
    error += if cross_chain_show(f) == 1 { 0 } else { 1 }

    // Fixed-width unsigned (FIXED_UNSIGNED -> UInt: cross-chain)
    g: CUInt8 := 7
    error += if cross_chain_show(g) == 2 { 0 } else { 1 }

    h: CUInt16 := 8
    error += if cross_chain_show(h) == 2 { 0 } else { 1 }

    i: CUInt32 := 9
    error += if cross_chain_show(i) == 2 { 0 } else { 1 }

    j: CUInt64 := 10
    error += if cross_chain_show(j) == 2 { 0 } else { 1 }

    // Standalone unsigned (-> UInt: cross-chain via family fallback)
    k: CSize := 11
    error += if cross_chain_show(k) == 2 { 0 } else { 1 }

    // --- Direct UFCS .classify() on cross-chain types (no generic wrapper) ---

    r: CInt32 := 42
    error += if r.classify() == 1 { 0 } else { 1 }

    s: CUInt8 := 7
    error += if s.classify() == 2 { 0 } else { 1 }

    t: CPtrDiff := 99
    error += if t.classify() == 1 { 0 } else { 1 }

    u: CSize := 88
    error += if u.classify() == 2 { 0 } else { 1 }

    // --- Pointer dereference then by-value dispatch ---
    // Ensures widening works on the dereferenced value, not on the pointer itself

    v:  CInt16      := 10
    vp: Ptr[CInt16] := v.&
    error += if cross_chain_show(vp.*) == 1 { 0 } else { 1 }

    w:  CUInt32      := 20
    wp: Ptr[CUInt32] := w.&
    error += if cross_chain_show(wp.*) == 2 { 0 } else { 1 }

    x:  CChar      := 65
    xp: Ptr[CChar] := x.&
    error += if cross_chain_show(xp.*) == 1 { 0 } else { 1 }

    y:  CSize      := 42
    yp: Ptr[CSize] := y.&
    error += if cross_chain_show(yp.*) == 2 { 0 } else { 1 }

    // --- Address-of on cross-chain types passed to explicit Ptr parameter ---
    // Ensures pointer-to-cross-chain-integer doesn't trigger widening

    z: CInt8 := 77
    error += if cross_chain_show(cross_chain_id_ptr(z.&)) == 1 { 0 } else { 1 }

    aa: CUInt16 := 88
    error += if cross_chain_show(cross_chain_id_ptr(aa.&)) == 2 { 0 } else { 1 }

    error
}

// === test_trait_builtin_family ===

family_show[T: Describe](x: T) -> String { x.describe() }

test_trait_builtin_family() {
    error := 0

    // CShort has module c"Int" — finds Int__describe__1 directly
    n: CShort := 42
    s         := family_show(n)
    error += if String.eq(s, String.from_cstr(c"42")) { 0 } else { 1 }

    // CInt also has module c"Int"
    m: CInt := 99
    s2      := family_show(m)
    error += if String.eq(s2, String.from_cstr(c"99")) { 0 } else { 1 }

    error
}

// === test_trait_builtin_user_trait ===

user_trait_show[T: Describe](x: T) -> String { x.describe() }

test_trait_builtin_user_trait() {
    error := 0

    // Int satisfies Describe
    s := user_trait_show(42)
    error += if String.eq(s, String.from_cstr(c"42")) { 0 } else { 1 }

    // Bool satisfies Describe
    sb := user_trait_show(true)
    error += if String.eq(sb, String.from_cstr(c"true")) { 0 } else { 1 }

    // Float satisfies Describe
    sf := user_trait_show(3.14)
    error += if String.len(sf) > 0 { 0 } else { 1 }

    error
}

// === main ===

main() {
    error := 0
    error += test_trait_builtin_all_numeric()
    error += test_trait_builtin_cross_chain()
    error += test_trait_builtin_family()
    error += test_trait_builtin_user_trait()
    error
}
