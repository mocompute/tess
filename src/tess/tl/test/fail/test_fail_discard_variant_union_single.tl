#module main

// Single-variant union — discard detection works for all tagged unions
Single: | Value { value: Float }

make_single() -> Single {
    Value(1.0)
}

// Test 1: three-variant union in stmt position is fine
test_single() -> CInt {
    make_single()
    0
}

main() {
    test_single()
    0
}
