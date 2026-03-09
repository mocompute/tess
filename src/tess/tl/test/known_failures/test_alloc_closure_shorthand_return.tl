#module main
#import <Alloc.tl>

// Shorthand return of an allocated closure (last expression without binding).
// Currently fails: escape checker requires explicit `f := ...; f` binding
// pattern to recognize allocated closures; shorthand is treated as a
// stack closure return and rejected with cannot_return_lambda.

make_adder(n: Int) -> (Int) -> Int {
    [[alloc, capture(n)]] (x) { x + n }
}

main() {
    add5 := make_adder(5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error
}
