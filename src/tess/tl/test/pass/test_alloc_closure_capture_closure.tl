#module main
#import <Alloc.tl>

// Capture another closure by value.

make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

apply_twice(f: (Int) -> Int, x: Int) -> (Int) -> Int {
    result := f(f(x))
    g := [[alloc, capture(result)]] (y) { result + y }
    g
}

main() {
    add5 := make_adder(5)
    // apply_twice(add5, 10) => add5(add5(10)) = add5(15) = 20
    // then captures result=20
    g := apply_twice(add5, 10)

    error := 0
    error += if g(0) == 20 { 0 } else { 1 }
    error += if g(2) == 22 { 0 } else { 1 }
    error
}
