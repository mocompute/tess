// Test nested generic specialization with Hash trait bounds.
#module Wrapper

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] { Wrapper(v = x) }

hash[T: Hash](w: Wrapper[T]) -> CSize { hash(w.v) }

#module main

// Multiple trait bounds
combine_hash[A: Hash, B: Hash](a: A, b: B) -> CSize {
    ha := hash(a)
    hb := hash(b)
    ha ^ hb
}

main() {
    error: CInt := 0

    // Basic wrapper hash
    w   := Wrapper.wrap(42)
    hw  := hash(w)
    h42 := hash(42)

    // Transparent wrapping: hash(Wrapper(v=42)) == hash(42)
    if hw != h42 { error += 1 }

    // Nested: Wrapper[Wrapper[Int]]
    ww  := Wrapper.wrap(w)
    hww := hash(ww)
    // hash(Wrapper(Wrapper(42))) == hash(Wrapper(42)) == hash(42)
    if hww != h42 { error += 1 }

    // Multiple trait bounds
    ch       := combine_hash(10, 20)
    expected := hash(10) ^ hash(20)
    if ch != expected { error += 1 }

    // Different values produce different nested hashes
    w2 := Wrapper.wrap(99)
    if hash(w2) == hw { error += 1 }

    error
}
