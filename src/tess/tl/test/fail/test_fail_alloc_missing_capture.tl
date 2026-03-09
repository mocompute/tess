#module main

// [[alloc]] on a closure that captures free variables but has no
// [[capture(...)]] list is invalid: the compiler needs an explicit
// capture list to know which variables to box.
// Expected error: alloc_missing_capture

main() {
    n := 5
    f := [[alloc]] (x) { x + n }
    f(10)
}
