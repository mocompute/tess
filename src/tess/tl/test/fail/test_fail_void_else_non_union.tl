#module main

// void-else on a non-tagged-union type should fail
test() -> CInt {
    x := 42
    x else err { return 1 }
    0
}

main() { test() }
