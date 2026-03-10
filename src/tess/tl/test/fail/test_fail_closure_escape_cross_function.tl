#module main

// Regression: escape check bindings must not leak across top-level functions.
// Function `a` binds `f` to an [[alloc]] closure.  Function `b` binds `f` to
// a plain stack closure and returns it — this must be rejected as closure_escape.
// Before the fix, the stale binding from `a` made `b`'s `f` look allocated.

a() -> Int {
    n := 1
    f := [[alloc, capture(n)]] (x) { x + n }
    f(1)
}

b() -> (Int) -> Int {
    n := 2
    f := (x) { x + n }
    f
}

main() {
    error := a()
    g     := b()
    error += g(1)
    r: CInt := error
    r
}
