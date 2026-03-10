#module main
#import <Alloc.tl>

// Stress test for allocated closures across many patterns.
// Currently fails: codegen produces undeclared identifiers when multiple
// allocated closures are created in the same non-main function body.

apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

// --- helpers that return allocated closures ---

make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

make_multiplier(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x * n }
    f
}

// Compose: evaluate f(g(x)) eagerly and capture the result
compose_at(f: (Int) -> Int, g: (Int) -> Int, x: Int) -> (Int) -> Int {
    val := f(g(x))
    h   := [[alloc, capture(val)]] (y) { val + y }
    h
}

// Test shadowed variable capture
test_shadow() -> CInt {
    n           := 10
    f           := [[alloc, capture(n)]] (x) { x + n }
    n           := 99
    g           := [[alloc, capture(n)]] (x) { x + n }
    error: CInt := 0
    error += if f(1) == 11 { 0 } else { 1 }
    error += if g(1) == 100 { 0 } else { 1 }
    error
}

// Test allocated closure with Bool capture and branching
test_bool_capture() -> CInt {
    flag := true
    f    := [[alloc, capture(flag)]] (x) {
        if flag { x } else { 0 - x }
    }
    flag2 := false
    g     := [[alloc, capture(flag2)]] (x) {
        if flag2 { x } else { 0 - x }
    }
    error: CInt := 0
    error += if f(42) == 42 { 0 } else { 1 }
    error += if g(42) == -42 { 0 } else { 1 }
    error
}

// Test deeply nested alloc closure creation (3 levels)
test_deep_nest() -> CInt {
    a := 1
    f := [[alloc, capture(a)]] (x) {
        b := x + a
        g := [[alloc, capture(b)]] (y) {
            c := y + b
            h := [[alloc, capture(c)]] (z) { z + c }
            h(y)
        }
        g(x)
    }
    // f(10): b=11, g(10): c=21, h(10): 10+21=31
    error: CInt := 0
    error += if f(10) == 31 { 0 } else { 1 }
    error
}

// Test alloc closure created inside a loop
test_loop_creation() -> CInt {
    sum := 0
    i   := 0
    while i < 5 {
        f := [[alloc, capture(i)]] (x) { x + i }
        sum += f(10)
        i += 1
    }
    // sum = (10+0)+(10+1)+(10+2)+(10+3)+(10+4) = 60
    if sum == 60 { 0 } else { 1 }
}

// Test explicit allocator in helper function
test_explicit_allocator() -> CInt {
    arena       := Alloc.bump_create(Alloc.context.default, 4096u)
    n           := 7
    f           := [[alloc(arena), capture(n)]] (x) { x + n }
    error: CInt := 0
    error += if f(3) == 10 { 0 } else { 1 }

    // Create another in the same arena
    m := 13
    g := [[alloc(arena), capture(m)]] (x) { x * m }
    error += if g(3) == 39 { 0 } else { 1 }
    error
}

main() {
    error: CInt := 0

    // Test 1: Allocated closure passed to a higher-order function
    add5 := make_adder(5)
    error += if apply(add5, 10) == 15 { 0 } else { 1 }

    // Test 2: Multiple allocated closures from the same factory
    add1 := make_adder(1)
    add2 := make_adder(2)
    add3 := make_adder(3)
    error += if add1(10) == 11 { 0 } else { 1 }
    error += if add2(10) == 12 { 0 } else { 1 }
    error += if add3(10) == 13 { 0 } else { 1 }
    error += if add1(0) == 1 { 0 } else { 1 }

    // Test 3: Composing allocated closures (eager evaluation)
    add10 := make_adder(10)
    mul3  := make_multiplier(3)
    // compose_at(add10, mul3, 5): val = add10(mul3(5)) = add10(15) = 25
    f := compose_at(add10, mul3, 5)
    error += if f(0) == 25 { 0 } else { 1 }
    error += if f(1) == 26 { 0 } else { 1 }

    // Test 4: Shadowed variables
    error += test_shadow()

    // Test 5: Bool capture and branching
    error += test_bool_capture()

    // Test 6: Deeply nested alloc closures (3 levels)
    error += test_deep_nest()

    // Test 7: Alloc closure created in a loop
    error += test_loop_creation()

    // Test 8: Explicit allocator
    error += test_explicit_allocator()

    error
}
