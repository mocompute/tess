// Test nested generic specialization with Hash trait bounds.
#import <Hash.tl>

#module Wrapper

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] { Wrapper(v = x) }

hash[T: Hash](w: Wrapper[T]) -> CSize { w.v.hash() }

#module main

// Multiple trait bounds
combine_hash[A: Hash, B: Hash](a: A, b: B) -> CSize {
    ha := a.hash()
    hb := b.hash()
    ha ^ hb
}

main() {
    error: CInt := 0

    // Basic wrapper hash
    w   := Wrapper.wrap(42)
    hw  := w.hash()
    n42: Int := 42
    h42 := n42.hash()

    // Transparent wrapping: hash(Wrapper(v=42)) == hash(42)
    if hw != h42 { error += 1 }

    // Nested: Wrapper[Wrapper[Int]]
    ww  := Wrapper.wrap(w)
    hww := ww.hash()
    // hash(Wrapper(Wrapper(42))) == hash(Wrapper(42)) == hash(42)
    if hww != h42 { error += 1 }

    // Multiple trait bounds
    ch := combine_hash(10, 20)
    a: Int := 10
    b: Int := 20
    expected := a.hash() ^ b.hash()
    if ch != expected { error += 1 }

    // Different values produce different nested hashes
    w2 := Wrapper.wrap(99)
    if w2.hash() == hw { error += 1 }

    error
}
