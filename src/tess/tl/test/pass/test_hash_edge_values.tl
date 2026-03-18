// Test hash edge values: zero, adjacent, negative, cross-value collision resistance.
#import <Hash.tl>

#module main

hash_from_other_function(v: Int) -> CSize { v.hash() }

main() {
    error: CInt := 0

    v0: Int  := 0
    v1: Int  := 1
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
