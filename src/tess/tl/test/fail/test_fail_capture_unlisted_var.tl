#module main

// Body uses variable `n` that is NOT listed in capture(...)
// Expected error: capture_unlisted_variable

main() {
    n := 5
    m := 10
    // captures m but body also uses n which is not listed
    f := [[alloc, capture(m)]] (x) { x + n + m }
    f(10)
}
