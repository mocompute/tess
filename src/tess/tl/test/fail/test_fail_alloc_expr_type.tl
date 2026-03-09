#module main

// [[alloc(expr)]] requires the expression to have type Ptr[Allocator].
// Using a non-allocator type must be rejected.
// Expected error: alloc_expr_type_mismatch

main() {
    n := 5
    f := [[alloc(n), capture(n)]] (x) { x + n }
    f(10)
}
