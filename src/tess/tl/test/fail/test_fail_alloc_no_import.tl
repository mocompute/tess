#module main

// [[alloc]] requires #import <Alloc.tl>.
// Using it without the import must be a compile-time error.
// Expected error: alloc_requires_import

main() {
    n := 10
    f := [[alloc, capture(n)]] (x) { x + n }
    f(5)
}
