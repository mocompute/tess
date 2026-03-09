#module main
#import <Alloc.tl>

// Storing an allocated closure in a struct field should be allowed
// because the context is heap-allocated and won't dangle.

Processor: { run: (Int) -> Int }

make_adder_processor(n: Int) -> Processor {
    f := [[alloc, capture(n)]] (x) { x + n }
    Processor(run = f)
}

main() {
    p := make_adder_processor(10)
    r := p.run(5)
    if r == 15 { 0 } else { 1 }
}
