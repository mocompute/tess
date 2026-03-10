#module main
#import <Alloc.tl>

// Shorthand return of an allocated closure (last expression without binding).
// Type checker accepts this, but transpiler doesn't handle a direct lambda
// as an implicit return value (emits FIXME_generate_expr).

make_adder(n: Int) -> (Int) -> Int {
    [[alloc, capture(n)]] (x) { x + n }
}

main() {
    add5        := make_adder(5)
    error: CInt := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error
}
