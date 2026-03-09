#module main
#import <Alloc.tl>

// Weak integer defaulting at the let-in lambda specialization boundary.
// The lambda parameter type is inferred as a weak int from the integer literal
// at the call site; the specialization lookup must default it to match.

main() {
    error: CInt := 0

    // Weak int from literal argument
    n := 100
    f := [[alloc, capture(n)]] (x) { x + n }
    error += if f(5) == 105 { 0 } else { 1 }

    // Unsigned weak int (literal is used in unsigned context)
    size: CUnsignedLongLong := 10
    g := [[alloc, capture(size)]] (x: CUnsignedLongLong) { x + size }
    error += if g(5) == 15 { 0 } else { 1 }

    // Multiple weak int params
    a := 1
    b := 2
    h := [[alloc, capture(a, b)]] (x) { x + a + b }
    error += if h(10) == 13 { 0 } else { 1 }

    error
}
