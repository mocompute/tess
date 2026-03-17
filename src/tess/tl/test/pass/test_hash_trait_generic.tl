// Test conditional conformance: a generic type with T: Hash can satisfy Hash.

#module Wrapper

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] {
    Wrapper(v = x)
}

hash[T: Hash](w: Wrapper[T]) -> CSize {
    hash(w.v)
}

#module main

compute_hash[T: Hash](x: T) -> CSize {
    hash(x)
}

main() -> CInt {
    error: CInt := 0

    w  := Wrapper.wrap(42)
    h1 := hash(w)
    error += if h1 != 0zu { 0 } else { 1 }

    // Same value should produce same hash
    w2 := Wrapper.wrap(42)
    h2 := hash(w2)
    error += if h1 == h2 { 0 } else { 1 }

    // Different value should (likely) produce different hash
    w3 := Wrapper.wrap(99)
    h3 := hash(w3)
    error += if h1 != h3 { 0 } else { 1 }

    // Via trait-bounded generic
    h4 := compute_hash(w)
    error += if h4 == h1 { 0 } else { 1 }

    error
}
