// Test: hash via UFCS (x.hash()) with userland Hash trait.
// Hash implementations are now in the standard library (Hash.tl).

#import <Hash.tl>

#module main

// Generic function using UFCS .hash() with Hash trait bound
compute_hash[T: Hash](x: T) -> CSize { x.hash() }

main() {
    error := 0

    // Int
    n: Int := 42
    h1 := n.hash()
    error += if h1 != 0zu { 0 } else { 1 }

    // Same value, same hash
    h2 := n.hash()
    error += if h1 == h2 { 0 } else { 1 }

    // Different value, different hash
    m: Int := 43
    h3 := m.hash()
    error += if h1 != h3 { 0 } else { 1 }

    // Bool
    b := true
    h4 := b.hash()
    error += if h4 != 0zu { 0 } else { 1 }

    // Float
    f: Float := 3.14
    h5 := f.hash()
    error += if h5 != 0zu { 0 } else { 1 }

    // Narrow type (CShort) — should use Int family, preserving sizeof(CShort)
    s: CShort := 7
    h6 := s.hash()
    error += if h6 != 0zu { 0 } else { 1 }

    // Via generic trait-bounded function
    h7 := compute_hash(n)
    error += if h7 == h1 { 0 } else { 1 }

    h8 := compute_hash(b)
    error += if h8 == h4 { 0 } else { 1 }

    error
}
