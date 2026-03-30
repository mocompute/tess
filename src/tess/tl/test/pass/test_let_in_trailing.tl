#module main

// Test: a binding at the end of a block (no body) evaluates to the bound value.

foo(value: CInt) {
    result: CInt := value
}

bar() {
    a      := 10
    b      := 20
    result := a + b
}

main() {
    foo(42) - 42 + bar() - 30
}
