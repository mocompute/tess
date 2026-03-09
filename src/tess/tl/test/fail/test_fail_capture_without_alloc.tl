#module main

// [[capture(...)]] without [[alloc]] is invalid: capture requires alloc
// because capture lists only make sense for heap-allocated closures.
// Expected error: capture_without_alloc

main() {
    n := 5
    f := [[capture(n)]] (x) { x + n }
    f(10)
}
