// Regression test: a generic function with unannotated parameters and an explicit
// return type annotation must correctly constrain the return type. Previously,
// parse_type bailed on the entire arrow when it encountered unannotated params,
// losing the return type and causing weak int defaulting to CLongLong.

#module Repro

T[K, V]: {
    key:   K,
    value: V,
    size:  CSize,
}

// Synopsis declares full types
_foo[K: Eq, V](entries: Ptr[T[K, V]], capacity: CSize, key: K, value: V) -> CSize

// Implementation omits types for entries, capacity — return type must still be CSize
_foo[K: Eq, V](entries, capacity, key: K, value: V) -> CSize {
    while true {
        return 1
    }
    0
}

bar[K: Eq, V](self: Ptr[T[K, V]], key: K, value: V) {
    delta := _foo[K, V](null, self->size, key, value)
    self->size += delta
}

#module main

main() -> CInt {
    t := Repro.T[CInt, CInt](key = 0, value = 0, size = 0)
    Repro.bar(t.&, 1, 100)
    0
}
