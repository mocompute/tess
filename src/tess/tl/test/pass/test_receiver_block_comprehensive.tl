// Comprehensive receiver block tests.
// Combined from: test_receiver_block.tl, test_receiver_block_constrained.tl,
// test_receiver_block_empty.tl, test_receiver_block_explicit_type_params.tl,
// test_receiver_block_extra_type_param.tl, test_receiver_block_generic.tl,
// test_receiver_block_mixed.tl, test_receiver_block_multi_param.tl,
// test_receiver_block_tagged_union_trait.tl

// --- From test_receiver_block.tl ---

#module Counter

Counter: { value: Int }

(self: Ptr[Counter]): {
    get() -> Int {
        self->value
    }

    set(v: Int) -> Void {
        self->value = v
    }

    increment() -> Void {
        self->value = self->value + 1
    }
}

// --- From test_receiver_block_constrained.tl ---

#module KeyVal

#import <Hash.tl>

KeyVal[K, V]: { key: K, val: V }

(kv: KeyVal[K: Hash, V]): {
    key_hash() -> CSize {
        kv.key.hash()
    }
}

// --- From test_receiver_block_explicit_type_params.tl ---

#module EPair

#import <Hash.tl>

EPair[A, B]: { first: A, second: B }

[T](): {
    make_epair(a: T, b: T) -> EPair[T, T] {
        EPair(first = a, second = b)
    }

    default_epair(v: T) -> EPair[T, T] {
        make_epair(v, v)
    }
}

[A, B](): {
    swap_epair(p: EPair[A, B]) -> EPair[B, A] {
        EPair(first = p.second, second = p.first)
    }
}

[A](): {
    map_first_epair[B](p: EPair[A, B], f: (A) -> A) -> EPair[A, B] {
        EPair(first = f(p.first), second = p.second)
    }
}

[K: Hash](): {
    hash_of_epair(k: K) -> CSize {
        k.hash()
    }
}

(p: EPair[A, B]): {
    get_first () -> A { p.first  }
    get_second() -> B { p.second }
}

// --- From test_receiver_block_extra_type_param.tl ---

#module MapBox

MapBox[T]: { value: T }

(b: MapBox[T]): {
    get() -> T {
        b.value
    }

    map[U](f: (T) -> U) -> MapBox[U] {
        MapBox[U](value = f(b.value))
    }
}

// --- From test_receiver_block_generic.tl ---

#module GenBox

GenBox[T]: { value: T }

(self: Ptr[GenBox[T]]): {
    get() -> T {
        self->value
    }

    set(v: T) -> Void {
        self->value = v
    }
}

// --- From test_receiver_block_mixed.tl ---

#module Point

Point: { x: Int, y: Int }

(p: Ptr[Point]): {
    sum() -> Int
}

(p: Ptr[Point]): {
    diff() -> Int {
        p->x - p->y
    }
}

sum(p) {
    p->x + p->y
}

// --- From test_receiver_block_multi_param.tl ---

#module MPair

MPair: { a: Int, b: Int }

(x: Ptr[MPair], y: Ptr[MPair]): {
    sum_a() -> Int {
        x->a + y->a
    }

    eq() -> Bool {
        x->a == y->a && x->b == y->b
    }
}

// --- From test_receiver_block_tagged_union_trait.tl ---

#module Foo

_A:  { x: CInt         }
_B:  { y: CInt         }
Foo: { | a: _A | b: _B }

(a: Ptr[Const[Foo]], b: Ptr[Const[Foo]]): {
    eq () -> Bool
}

eq(a, b) { true }

// --- main module ---

#module main

#import <Hash.tl>

// From test_receiver_block_empty.tl
(): {
    fixed() { 42 }
}

// From test_receiver_block_tagged_union_trait.tl
check[T: Eq](a: T, b: T) -> Bool { a == b }

// Basic receiver block: single parameter, non-generic.
test_receiver_block() {
    error := 0
    c     := Counter(value = 0)

    error += if c.get() == 0 {0} else {1}

    c.set(42)
    error += if c.get() == 42 {0} else {1}

    c.increment()
    error += if c.get() == 43 {0} else {1}

    error
}

// Receiver block with trait constraints on inferred type parameters.
test_receiver_block_constrained() {
    error := 0

    kv := KeyVal[Int, Bool](key = 42, val = true)
    h  := kv.key_hash()
    error += if h != 0 {0} else {1}

    error
}

// Receiver block with no parameters.
test_receiver_block_empty() {
    fixed() - 42
}

// Receiver block with explicit type parameters for zero-parameter blocks.
test_receiver_block_explicit_type_params() {
    error := 0

    // Single type param
    p := EPair.make_epair(1, 2)
    error += if p.first == 1 && p.second == 2 {0} else {1}

    // default_epair
    p2 := EPair.default_epair(7)
    error += if p2.first == 7 && p2.second == 7 {0} else {1}

    // Multiple type params
    p3 := EPair.make_epair(10, 20)
    p4 := EPair.swap_epair(p3)
    error += if p4.first == 20 && p4.second == 10 {0} else {1}

    // Entry-level additional type param
    p5: EPair[Int, Bool] := EPair(first = 3, second = true)
    p6                   := EPair.map_first_epair(p5, (x) { x * 10 })
    error += if p6.first == 30 && p6.second == true {0} else {1}

    // Trait-constrained explicit type param
    h := EPair.hash_of_epair(42)
    error += if h != 0 {0} else {1}

    // Standard receiver block coexistence
    error += if p.get_first() == 1 {0} else {1}
    error += if p.get_second() == 2 {0} else {1}

    error
}

// Receiver block where functions introduce additional type parameters.
test_receiver_block_extra_type_param() {
    error := 0

    b := MapBox[Int](value = 10)
    error += if b.get() == 10 {0} else {1}

    b2 := b.map((x) { x > 5 })
    error += if b2.get() == true {0} else {1}

    error
}

// Receiver block with generic type parameter inference.
test_receiver_block_generic() {
    error := 0

    b := GenBox[Int](value = 10)
    error += if b.get() == 10 {0} else {1}

    b.set(42)
    error += if b.get() == 42 {0} else {1}

    error
}

// Receiver block with forward declarations, definitions, and multiple blocks.
test_receiver_block_mixed() {
    error := 0
    p     := Point(x = 3, y = 7)

    error += if p.sum() == 10 {0} else {1}
    error += if p.diff() == -4 {0} else {1}

    error
}

// Receiver block with multiple parameters.
test_receiver_block_multi_param() {
    error := 0
    p1    := MPair.MPair(a = 1, b = 2)
    p2    := MPair.MPair(a = 3, b = 4)
    p3    := MPair.MPair(a = 1, b = 2)

    error += if MPair.sum_a(p1.&, p2.&) == 4 {0} else {1}
    error += if MPair.eq(p1.&, p3.&) == true {0} else {1}
    error += if MPair.eq(p1.&, p2.&) == false {0} else {1}

    error
}

// Receiver block on tagged union with trait bounds.
test_receiver_block_tagged_union_trait() {
    a: Foo := void
    b: Foo := void
    if check(a, b) { 0 } else { 1 }
}

main() {
    error := 0
    error += test_receiver_block()
    error += test_receiver_block_constrained()
    error += test_receiver_block_empty()
    error += test_receiver_block_explicit_type_params()
    error += test_receiver_block_extra_type_param()
    error += test_receiver_block_generic()
    error += test_receiver_block_mixed()
    error += test_receiver_block_multi_param()
    error += test_receiver_block_tagged_union_trait()
    error
}
