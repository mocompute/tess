// Receiver block with explicit type parameters for zero-parameter blocks.
// Syntax: [T](): { ... } — desugars [T] onto each entry.

#module Pair

#import <Hash.tl>

Pair[A, B]: { first: A, second: B }

// Single explicit type param — constructors and by-value functions.
[T](): {
    make_pair(a: T, b: T) -> Pair[T, T] {
        Pair(first = a, second = b)
    }

    default_pair(v: T) -> Pair[T, T] {
        make_pair(v, v)
    }
}

// Multiple explicit type params.
[A, B](): {
    swap(p: Pair[A, B]) -> Pair[B, A] {
        Pair(first = p.second, second = p.first)
    }
}

// Entry-level type params merged with block-level.
// map[B] inside [A]() desugars to map[A, B].
[A](): {
    map_first[B](p: Pair[A, B], f: (A) -> A) -> Pair[A, B] {
        Pair(first = f(p.first), second = p.second)
    }
}

// Trait-constrained explicit type param.
// [K: Hash]() desugars to hash_of[K: Hash](...).
[K: Hash](): {
    hash_of(k: K) -> CSize {
        k.hash()
    }
}

// Coexistence with standard receiver blocks in the same module.
(p: Pair[A, B]): {
    get_first () -> A { p.first  }
    get_second() -> B { p.second }
}

#module main
#import <Hash.tl>

main() {
    error := 0

    // Single type param
    p := Pair.make_pair(1, 2)
    error += if p.first == 1 && p.second == 2 {0} else {1}

    // default_pair
    p2 := Pair.default_pair(7)
    error += if p2.first == 7 && p2.second == 7 {0} else {1}

    // Multiple type params
    p3 := Pair.make_pair(10, 20)
    p4 := Pair.swap(p3)
    error += if p4.first == 20 && p4.second == 10 {0} else {1}

    // Entry-level additional type param
    p5: Pair[Int, Bool] := Pair(first = 3, second = true)
    p6                  := Pair.map_first(p5, (x) { x * 10 })
    error += if p6.first == 30 && p6.second == true {0} else {1}

    // Trait-constrained explicit type param
    h := Pair.hash_of(42)
    error += if h != 0 {0} else {1}

    // Standard receiver block coexistence
    error += if p.get_first() == 1 {0} else {1}
    error += if p.get_second() == 2 {0} else {1}

    error
}
