#module main

// capture list names a variable that doesn't exist in the enclosing scope
// Expected error: capture_not_in_scope

main() {
    n := 5
    // 'nonexistent' is not defined anywhere
    f := [[alloc, capture(n, nonexistent)]] (x) { x + n }
    f(10)
}
