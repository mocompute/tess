#module main

// Constructing a generic struct with null for a function pointer field
// that references a generic type parameter should resolve the type.

Foo[K]: {
    f: (Ptr[K]) -> UInt,
}

main() {
    opts := Foo[Int](f = null)
    if opts.f == null { return 0 }
    1
}
