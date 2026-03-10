#module main

// --- Helper structs ---
Pair:   { a: Int, b: Int }
Box[T]: { val: T         }

pair_sum(p: Pair) -> Int { p.a + p.b }

// --- Generic helpers ---

// Level 1: apply a function to a value
apply1[T](val: T, f: (T) -> Int) -> Int { f(val) }

// Level 2: apply a function, then apply another function to the result
apply2[A, B](val: A, f: (A) -> B, g: (B) -> Int) -> Int { g(f(val)) }

// Generic function that takes a closure AND a value of the same type
reduce[T](a: T, b: T, f: (T, T) -> T) -> T { f(a, b) }

// Identity
identity[T](x: T) -> T { x }

// Transform via function pointer
transform[T](val: T, f: (T) -> T) -> T { f(val) }

main() {
    error := 0

    // Test 1: Deeply nested generic function calls with closures (3+ levels)
    // apply1 -> closure -> apply1 -> closure -> identity
    ( {
     x := 10
        // Outer closure captures x, calls apply1 with another closure inside
     outer := (v: Int) {
     y     := v + x
     inner := (w: Int) { identity(w) + 1 }
     apply1(y, inner)
     }
     result := apply1(5, outer)
        // v=5, y=5+10=15, inner(15)=identity(15)+1=16
     error += if result == 16 { 0 } else { 1 }
     void
     })

    // Test 2: Same test with Pair type (different specialization)
    ( {
     offset := 100
     f      := (p: Pair) {
     inner  := (q: Pair) { q.a + q.b + offset }
     apply1(p, inner)
     }
     p      := Pair(a = 3, b = 7)
     result := apply1(p, f)
        // f(Pair(3,7)) -> inner(Pair(3,7)) -> 3+7+100 = 110
     error += if result == 110 { 0 } else { 1 }
     void
     })

    // Test 3: Generic reduce specialized at multiple types
    ( {
     r1 := reduce(10, 20, (a: Int, b: Int) { a + b })
     error += if r1 == 30 { 0 } else { 1 }

        r2 := reduce(true, false, (a: Bool, b: Bool) { a || b })
        error += if r2 == true { 0 } else { 1 }

        r3 := reduce(false, false, (a: Bool, b: Bool) { a || b })
        error += if r3 == false { 0 } else { 1 }

        p1 := Pair(a = 1, b = 2)
        p2 := Pair(a = 3, b = 4)
        r4 := reduce(p1, p2, (a: Pair, b: Pair) { Pair(a = a.a + b.a, b = a.b + b.b) })
        error += if r4.a == 4 { 0 } else { 1 }
        error += if r4.b == 6 { 0 } else { 1 }
        void
    })

    // Test 4: Multiple closures from same generic function, different specializations
    // All created before any are called
    ( {
     v1 := 42
     v2 := true
     v3 := Pair(a = 5, b = 6)

        f1 := (x: Int) { x + 1 }
        f2 := (b: Bool) { if b { 10 } else { 20 } }
        f3 := (p: Pair) { p.a * p.b }

        r1 := apply1(v1, f1)
        r2 := apply1(v2, f2)
        r3 := apply1(v3, f3)

        error += if r1 == 43 { 0 } else { 1 }
        error += if r2 == 10 { 0 } else { 1 }
        error += if r3 == 30 { 0 } else { 1 }
        void
    })

    // Test 5: Closures capturing different values, all passed to same generic fn
    ( {
     a := 100
     b := 200
     c := 300

        fa := (x: Int) { x + a }
        fb := (x: Int) { x + b }
        fc := (x: Int) { x + c }

        r1 := apply1(1, fa)
        r2 := apply1(1, fb)
        r3 := apply1(1, fc)

        error += if r1 == 101 { 0 } else { 1 }
        error += if r2 == 201 { 0 } else { 1 }
        error += if r3 == 301 { 0 } else { 1 }
        void
    })

    // Test 6: Generic closure stored in a generic struct field
    ( {
     b1 := Box(val = 42)
     r1 := apply1(b1, (bx: Box[Int]) { bx.val + 1 })
     error += if r1 == 43 { 0 } else { 1 }

        b2 := Box(val = true)
        r2 := apply1(b2, (bx: Box[Bool]) { if bx.val { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }

        // Nested Box
        b3 := Box(val = Box(val = 99))
        r3 := apply1(b3, (bx: Box[Box[Int]]) { bx.val.val })
        error += if r3 == 99 { 0 } else { 1 }
        void
    })

    // Test 7: apply2 - two-step generic pipeline with closures
    ( {
        // Int -> Bool -> Int
     r1 := apply2(42, (x: Int) { x > 0 }, (b: Bool) { if b { 1 } else { 0 } })
     error += if r1 == 1 { 0 } else { 1 }

        // Pair -> Int -> Int
        p  := Pair(a = 3, b = 4)
        r2 := apply2(p, (pp: Pair) { pp.a + pp.b }, (x: Int) { x * 2 })
        error += if r2 == 14 { 0 } else { 1 }

        // Bool -> Int -> Int
        r3 := apply2(false, (b: Bool) { if b { 10 } else { 20 } }, (x: Int) { x + 5 })
        error += if r3 == 25 { 0 } else { 1 }
        void
    })

    // Test 8: transform chain - same generic fn applied repeatedly
    ( {
     add1 := (x: Int) { x + 1 }
     r    := transform(transform(transform(0, add1), add1), add1)
     error += if r == 3 { 0 } else { 1 }

        // With Pair - transform twice
        shift := (p: Pair) { Pair(a = p.a + 1, b = p.b + 1) }
        p     := Pair(a = 0, b = 0)
        rp    := transform(transform(p, shift), shift)
        error += if rp.a == 2 { 0 } else { 1 }
        error += if rp.b == 2 { 0 } else { 1 }
        void
    })

    // Test 9: Closure capturing mutable var + generic reduce
    ( {
     sum        := 0
     accumulate := (a: Int, b: Int) {
     sum = sum + a + b
     a + b
     }
     r := reduce(10, 20, accumulate)
     error += if r == 30 { 0 } else { 1 }
     error += if sum == 30 { 0 } else { 1 }

        r2 := reduce(3, 7, accumulate)
        error += if r2 == 10 { 0 } else { 1 }
        error += if sum == 40 { 0 } else { 1 }
        void
    })

    // Test 10: Deeply nested - 3 levels of generic fn + closure interleaving
    ( {
     base   := 1000
     result := apply1(5, (a: Int) {
     apply1(a + 10, (b: Int) {
     apply1(b + 100, (c: Int) {
     c + base
     })
     })
     })
        // a=5, b=5+10=15, c=15+100=115, result=115+1000=1115
     error += if result == 1115 { 0 } else { 1 }
     void
     })

    error
}
