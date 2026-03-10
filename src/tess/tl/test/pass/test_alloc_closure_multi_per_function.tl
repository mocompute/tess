#module main
#import <Alloc.tl>

// Multiple allocated closures in the same non-main function body.
// Currently fails: codegen generates undeclared identifiers for the
// closure function pointers when more than one alloc closure exists
// in a single function scope.

test() -> CInt {
    n           := 10
    f           := [[alloc, capture(n)]] (x) { x + n }
    m           := 20
    g           := [[alloc, capture(m)]] (x) { x + m }
    error: CInt := 0
    error += if f(1) == 11 { 0 } else { 1 }
    error += if g(1) == 21 { 0 } else { 1 }
    error
}

main() { test() }
