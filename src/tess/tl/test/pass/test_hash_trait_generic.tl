// Test conditional conformance: a generic type with T: Hash can satisfy Hash.

#import <Hash.tl>

#module Wrapper

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] {
    Wrapper(v = x)
}

hash[T: Hash](w: Wrapper[T]) -> CSize {
    w.v.hash()
}

#module main

compute_hash[T: Hash](x: T) -> CSize {
    x.hash()
}

main() -> CInt {
    error: CInt := 0

    w  := Wrapper.wrap(42)
    h1 := w.hash()
    error += if h1 != 0zu { 0 } else { 1 }

    // Same value should produce same hash
    w2 := Wrapper.wrap(42)
    h2 := w2.hash()
    error += if h1 == h2 { 0 } else { 1 }

    // Different value should (likely) produce different hash
    w3 := Wrapper.wrap(99)
    h3 := w3.hash()
    error += if h1 != h3 { 0 } else { 1 }

    // Via trait-bounded generic
    h4 := compute_hash(w)
    error += if h4 == h1 { 0 } else { 1 }

    error
}
