#module main
#import <Alloc.tl>

// Use [[alloc(expr), capture(...)]] with an explicit Ptr[Allocator].

make_adder(n: Int, a: Ptr[Alloc.Allocator]) -> (Int) -> Int {
    f := [[alloc(a), capture(n)]] (x) { x + n }
    f
}

main() {
    // Use the default allocator explicitly
    a     := Alloc.context.default
    add10 := make_adder(10, a)

    error := 0
    error += if add10(5) == 15 { 0 } else { 1 }
    error += if add10(32) == 42 { 0 } else { 1 }
    error
}
