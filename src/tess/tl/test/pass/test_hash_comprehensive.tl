// Comprehensive hash tests combining: edge values, module functions, nested generics,
// builtin trait, generic trait, user type trait, and UFCS proof-of-concept.

#import <Hash.tl>
#import <String.tl>
#import <cstring.tl>

// --- From test_hash_nested_generic.tl ---
#module NestedWrapper

NestedWrapper[T]: { v: T }

wrap[T](x: T) -> NestedWrapper[T] { NestedWrapper(v = x) }

hash[T: Hash](w: NestedWrapper[T]) -> CSize { w.v.hash() }

// --- From test_hash_trait_generic.tl ---
#module GenericWrapper

GenericWrapper[T]: { v: T }

wrap[T](x: T) -> GenericWrapper[T] {
    GenericWrapper(v = x)
}

hash[T: Hash](w: GenericWrapper[T]) -> CSize {
    w.v.hash()
}

// --- main module ---
#module main

// Shared generic helper (used by builtin, generic, user_type, ufcs_poc tests)
compute_hash[T: Hash](x: T) -> CSize {
    x.hash()
}

// From test_hash_edge_values.tl
hash_from_other_function(v: Int) -> CSize { v.hash() }

// From test_hash_nested_generic.tl
combine_hash[A: Hash, B: Hash](a: A, b: B) -> CSize {
    ha := a.hash()
    hb := b.hash()
    ha ^ hb
}

// --- test_hash_edge_values ---
test_hash_edge_values() {
    error: CInt := 0

    v0:  Int := 0
    v1:  Int := 1
    vm1: Int := -1
    v42: Int := 42

    h0  := v0.hash()
    h1  := v1.hash()
    hm1 := vm1.hash()
    h42 := v42.hash()

    // hash(0) should be non-zero (FNV-1a offset basis)
    if h0 == 0zu { error += 1 }

    // Adjacent values should differ
    if h0 == h1 { error += 1 }

    // hash(-1) should work and differ from hash(1)
    if hm1 == h1 { error += 1 }

    // Consistency: same value from different call sites
    h42b := hash_from_other_function(42)
    if h42 != h42b { error += 1 }

    // Different values should produce different hashes
    if h42 == h0 { error += 1 }
    if h42 == h1 { error += 1 }

    error
}

// --- test_hash_module ---
test_hash_module() {
    error: CInt := 0

    // Hash.fnv1a on a raw buffer matches hash for the same content.
    s      := String.from_cstr(c"hello")
    h_str  := s.hash()
    p      := String.cstr(s.&)
    h_hash := Hash.fnv1a(p, String.len(s))
    error += if h_str == h_hash { 0 } else { 1 }

    // Non-zero sanity check.
    error += if h_hash != 0zu { 0 } else { 1 }

    // Different input produces different hash.
    s2     := String.from_cstr(c"world")
    h2_str := s2.hash()
    h2_raw := Hash.fnv1a(String.cstr(s2.&), String.len(s2))
    error += if h2_str == h2_raw { 0 } else { 1 }
    error += if h_hash != h2_raw { 0 } else { 1 }

    // Empty buffer matches hash(String.empty()).
    e                  := String.empty()
    h_empty            := e.hash()
    null_p: Ptr[CChar] := c""
    h_raw_empty        := Hash.fnv1a(null_p, 0zu)
    error += if h_empty == h_raw_empty { 0 } else { 1 }

    // Hash.fnv1a_cstring matches hash for the same content.
    h_cstr := Hash.fnv1a_cstring(c"hello")
    error += if h_cstr == h_str { 0 } else { 1 }

    error
}

// --- test_hash_nested_generic ---
test_hash_nested_generic() {
    error: CInt := 0

    // Basic wrapper hash
    w        := NestedWrapper.wrap(42)
    hw       := w.hash()
    n42: Int := 42
    h42      := n42.hash()

    // Transparent wrapping: hash(NestedWrapper(v=42)) == hash(42)
    if hw != h42 { error += 1 }

    // Nested: NestedWrapper[NestedWrapper[Int]]
    ww  := NestedWrapper.wrap(w)
    hww := ww.hash()
    // hash(NestedWrapper(NestedWrapper(42))) == hash(NestedWrapper(42)) == hash(42)
    if hww != h42 { error += 1 }

    // Multiple trait bounds
    ch       := combine_hash(10, 20)
    a: Int   := 10
    b: Int   := 20
    expected := a.hash() ^ b.hash()
    if ch != expected { error += 1 }

    // Different values produce different nested hashes
    w2 := NestedWrapper.wrap(99)
    if w2.hash() == hw { error += 1 }

    error
}

// --- test_hash_trait_builtin ---
test_hash_trait_builtin() {
    error: CInt := 0

    // Int
    n1: Int := 42
    h1      := n1.hash()
    error += if h1 != 0 { 0 } else { 1 }

    // Float
    f1: Float := 3.14
    h2        := f1.hash()
    error += if h2 != 0 { 0 } else { 1 }

    // Bool
    bt := true
    bf := false
    h3 := bt.hash()
    h4 := bf.hash()
    error += if h3 != h4 { 0 } else { 1 }

    // CInt
    ci: CInt := 7
    h5       := ci.hash()
    error += if h5 != 0 { 0 } else { 1 }

    // CSize
    cs: CSize := 99
    h6        := cs.hash()
    error += if h6 != 0 { 0 } else { 1 }

    // CString (Ptr[CChar])
    s: CString := c"hello"
    h7         := s.hash()
    error += if h7 != 0 { 0 } else { 1 }

    // CChar
    c: CChar := 'h'
    h10      := c.hash()
    error += if h10 != 0 { 0 } else { 1 }

    // Same value should produce same hash
    n2: Int := 42
    h8      := n2.hash()
    error += if h1 == h8 { 0 } else { 1 }

    // Via generic trait-bounded function
    h9 := compute_hash(n1)
    error += if h9 == h1 { 0 } else { 1 }

    error
}

// --- test_hash_trait_generic ---
test_hash_trait_generic() {
    error: CInt := 0

    w  := GenericWrapper.wrap(42)
    h1 := w.hash()
    error += if h1 != 0zu { 0 } else { 1 }

    // Same value should produce same hash
    w2 := GenericWrapper.wrap(42)
    h2 := w2.hash()
    error += if h1 == h2 { 0 } else { 1 }

    // Different value should (likely) produce different hash
    w3 := GenericWrapper.wrap(99)
    h3 := w3.hash()
    error += if h1 != h3 { 0 } else { 1 }

    // Via trait-bounded generic
    h4 := compute_hash(w)
    error += if h4 == h1 { 0 } else { 1 }

    error
}

// --- test_hash_trait_user_type ---
test_hash_trait_user_type() {
    error: CInt := 0

    // String has a hash() implementation
    s  := String.from_cstr(c"hello")
    h1 := s.hash()
    error += if h1 != 0zu { 0 } else { 1 }

    // Same string should produce same hash
    s2 := String.from_cstr(c"hello")
    h2 := s2.hash()
    error += if h1 == h2 { 0 } else { 1 }

    // Different string should (likely) produce different hash
    s3 := String.from_cstr(c"world")
    h3 := s3.hash()
    error += if h1 != h3 { 0 } else { 1 }

    // Via generic trait-bounded function
    h4 := compute_hash(s)
    error += if h4 == h1 { 0 } else { 1 }

    error
}

// --- test_hash_ufcs_poc ---
test_hash_ufcs_poc() {
    error := 0

    // Int
    n: Int := 42
    h1     := n.hash()
    error += if h1 != 0zu { 0 } else { 1 }

    // Same value, same hash
    h2 := n.hash()
    error += if h1 == h2 { 0 } else { 1 }

    // Different value, different hash
    m: Int := 43
    h3     := m.hash()
    error += if h1 != h3 { 0 } else { 1 }

    // Bool
    b  := true
    h4 := b.hash()
    error += if h4 != 0zu { 0 } else { 1 }

    // Float
    f: Float := 3.14
    h5       := f.hash()
    error += if h5 != 0zu { 0 } else { 1 }

    // Narrow type (CShort) — should use Int family, preserving sizeof(CShort)
    s: CShort := 7
    h6        := s.hash()
    error += if h6 != 0zu { 0 } else { 1 }

    // Via generic trait-bounded function
    h7 := compute_hash(n)
    error += if h7 == h1 { 0 } else { 1 }

    h8 := compute_hash(b)
    error += if h8 == h4 { 0 } else { 1 }

    error
}

// --- main: run all tests ---
main() {
    error: CInt := 0
    error += test_hash_edge_values()
    error += test_hash_module()
    error += test_hash_nested_generic()
    error += test_hash_trait_builtin()
    error += test_hash_trait_generic()
    error += test_hash_trait_user_type()
    error += test_hash_ufcs_poc()
    error
}
