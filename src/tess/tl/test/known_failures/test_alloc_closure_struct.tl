#module main

// Storing an allocated closure in a struct field should be allowed
// because the context is heap-allocated and won't dangle.

Processor[T]: { run: (T) -> T }

make_adder_processor(n: Int) -> Processor[Int] {
    f := [[alloc, capture(n)]] (x) { x + n }
    Processor[Int](run = f)
}

main() {
    p := make_adder_processor(10)
    r := p.run(5)
    if r == 15 { 0 } else { 1 }
}
