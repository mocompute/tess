#module main

// Regression: fatal() message containing '%' must not be interpreted
// as a printf format specifier. Before the fix, this was UB.

conditionally_fail(should_fail: Bool) {
    if should_fail {
        _tl_fatal_(c"100% done")
    }
    void
}

main() {
    conditionally_fail(false)
    0
}
