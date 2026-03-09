#module main

// capture(n) lists `n` but body doesn't use it
// Expected error: capture_unused_variable

main() {
    n := 5
    // captures n but body doesn't use it
    f := [[alloc, capture(n)]] (x) { x + 1 }
    f(10)
}
