#module main

// A generic struct with a function pointer field whose parameter type
// uses a generic type wrapped in Ptr[K] should parse successfully.

Foo[K]: {
    f: (Ptr[K]) -> UInt,
}

main() {
    0
}
