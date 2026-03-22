#module main
#import <Alloc.tl>

// Storing an allocated closure in a generic struct field should work.
// Currently fails: Processor[Int] generates as unspecialized c"Processor" in some C codegen paths.

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
