#module main
#import <Alloc.tl>

// Generic closure with captures, specialized with different types.

make_adder[T](n: T) -> (T) -> T {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

main() {
    add5  := make_adder[Int](5)
    add1p := make_adder[Float](1.5)
    error := 0
    error += if add5(10) == 15 { 0 } else { 1 }
    error += if add1p(2.5) == 4.0 { 0 } else { 1 }
    error
}
