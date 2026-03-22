// Test that hash() works on built-in types via userland Hash trait.

#import <Hash.tl>

#module main

compute_hash[T: Hash](x: T) -> CSize {
    x.hash()
}

main() -> CInt {
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
