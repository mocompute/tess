// Test that hash() works on built-in types via the _tl_hash_ intrinsic.

#module main

compute_hash[T: Hash](x: T) -> CSize {
    hash(x)
}

main() -> CInt {
    error: CInt := 0

    // Int
    h1 := hash(42)
    error += if h1 != 0zu { 0 } else { 1 }

    // Float
    h2 := hash(3.14)
    error += if h2 != 0zu { 0 } else { 1 }

    // Bool
    h3 := hash(true)
    h4 := hash(false)
    error += if h3 != h4 { 0 } else { 1 }

    // CInt
    ci: CInt := 7
    h5       := hash(ci)
    error += if h5 != 0zu { 0 } else { 1 }

    // CSize
    cs: CSize := 99zu
    h6        := hash(cs)
    error += if h6 != 0zu { 0 } else { 1 }

    // CString (Ptr[CChar])
    s: CString := "hello"
    h7         := hash(s)
    error += if h7 != 0zu { 0 } else { 1 }

    // Same value should produce same hash
    h8 := hash(42)
    error += if h1 == h8 { 0 } else { 1 }

    // Via generic trait-bounded function
    h9 := compute_hash(42)
    error += if h9 == h1 { 0 } else { 1 }

    error
}
