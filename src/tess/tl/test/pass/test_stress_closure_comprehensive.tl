// Combined stress test for closures.
// Merged from: test_stress_alloc_closures, test_stress_closures,
// test_stress_generic_closures, test_stress_generic_closure_adversarial,
// test_stress_recursive_generic_closures,
// test_stress_recursive_generic_closures_adversarial.

#module main
#import <Alloc.tl>

// ============================================================
// From: test_stress_alloc_closures
// ============================================================

ac_apply(f: (Int) -> Int, x: Int) -> Int { f(x) }

make_adder(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x + n }
    f
}

make_multiplier(n: Int) -> (Int) -> Int {
    f := [[alloc, capture(n)]] (x) { x * n }
    f
}

compose_at(f: (Int) -> Int, g: (Int) -> Int, x: Int) -> (Int) -> Int {
    val := f(g(x))
    h   := [[alloc, capture(val)]] (y) { val + y }
    h
}

ac_test_shadow() -> CInt {
    n           := 10
    f           := [[alloc, capture(n)]] (x) { x + n }
    n           := 99
    g           := [[alloc, capture(n)]] (x) { x + n }
    error: CInt := 0
    error += if f(1) == 11 { 0 } else { 1 }
    error += if g(1) == 100 { 0 } else { 1 }
    error
}

ac_test_bool_capture() -> CInt {
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

ac_test_deep_nest() -> CInt {
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
    error: CInt := 0
    error += if f(10) == 31 { 0 } else { 1 }
    error
}

ac_test_loop_creation() -> CInt {
    sum := 0
    i   := 0
    while i < 5 {
        f := [[alloc, capture(i)]] (x) { x + i }
        sum += f(10)
        i += 1
    }
    if sum == 60 { 0 } else { 1 }
}

ac_test_explicit_allocator() -> CInt {
    arena       := Alloc.BumpAllocator.create(Alloc.context.default, 4096zu)
    n           := 7
    f           := [[alloc(arena), capture(n)]] (x) { x + n }
    error: CInt := 0
    error += if f(3) == 10 { 0 } else { 1 }
    m := 13
    g := [[alloc(arena), capture(m)]] (x) { x * m }
    error += if g(3) == 39 { 0 } else { 1 }
    error
}

test_alloc_closures() {
    error: CInt := 0

    add5 := make_adder(5)
    error += if ac_apply(add5, 10) == 15 { 0 } else { 1 }

    add1 := make_adder(1)
    add2 := make_adder(2)
    add3 := make_adder(3)
    error += if add1(10) == 11 { 0 } else { 1 }
    error += if add2(10) == 12 { 0 } else { 1 }
    error += if add3(10) == 13 { 0 } else { 1 }
    error += if add1(0) == 1 { 0 } else { 1 }

    add10 := make_adder(10)
    mul3  := make_multiplier(3)
    f     := compose_at(add10, mul3, 5)
    error += if f(0) == 25 { 0 } else { 1 }
    error += if f(1) == 26 { 0 } else { 1 }

    error += ac_test_shadow()
    error += ac_test_bool_capture()
    error += ac_test_deep_nest()
    error += ac_test_loop_creation()
    error += ac_test_explicit_allocator()

    error
}

// ============================================================
// From: test_stress_closures
// ============================================================

ClosureShape: | ClosureCircle { radius: Int }
              | ClosureSquare { length: Int }

cl_call_in_when(s: ClosureShape, f: () -> Int) -> Int {
    when s {
        c:  ClosureCircle { f() + c.radius }
        sq: ClosureSquare { f() + sq.length }
    }
}

cl_apply(f: () -> Int) -> Int { f() }

test_closures() {
    error := 0

    // Test 1: Lambda capturing a variable that is later shadowed
    ( {
     x := 10
     f := () { x }
     x := 20
     g := () { x }
     error += if f() == 10 {0} else {1}
     error += if g() == 20 {0} else {1}
     void
     })

    // Test 2: Lambda capturing a when-bound variable
    ( {
     s      := ClosureCircle(7)
     result := when s {
     c: ClosureCircle {
     val := c.radius
     f   := () { val }
     f() * 2
     }
     sq: ClosureSquare { 0 }
     }
     error += if result == 14 {0} else {1}
     void
     })

    // Test 3: Multiple lambdas capturing the same mutable variable
    ( {
     counter := 0
     inc     := () { counter = counter + 1 }
     get     := () { counter }
     inc()
     inc()
     inc()
     error += if get() == 3 {0} else {1}
     void
     })

    // Test 4: Passing a closure to a function that calls it inside a when arm
    ( {
     x      := 100
     f      := () { x }
     s      := ClosureCircle(5)
     result := cl_call_in_when(s, f)
     error += if result == 105 {0} else {1}
     void
     })

    // Test 5: Closure that mutates captured variable, called multiple times
    ( {
     total := 0
     add   := (n: Int) { total = total + n }
     add(10)
     add(20)
     add(30)
     error += if total == 60 {0} else {1}
     void
     })

    // Test 6: Nested closures - closure inside a closure
    ( {
     x     := 5
     outer := () {
     y     := x + 10
     inner := () { y }
     inner()
     }
     result := cl_apply(outer)
     error += if result == 15 {0} else {1}
     void
     })

    // Test 7: Closure captured before a loop, sees mutations after loop
    ( {
     sum := 0
     f   := () { sum }
     i   := 0
     while i < 5, i = i + 1 {
     sum            = sum + i
     }
     error += if f() == 10 {0} else {1}
     void
     })

    error
}

// ============================================================
// From: test_stress_generic_closures
// ============================================================

GCPair: { a: Int, b: Int }

gc_pair_sum(p: GCPair) -> Int { p.a + p.b }

gc_apply_to[T](val: T, f: (T) -> Int) -> Int { f(val) }

gc_run_thunk[T](f: () -> T) -> T { f() }

gc_combine[T](val: T, f: (T) -> Int, g: (T) -> Int) -> Int {
    f(val) + g(val)
}

gc_identity[T](x: T) -> T { x }

gc_map_pair[A, B](val: A, f: (A) -> B) -> B { f(val) }

test_generic_closures() {
    error := 0

    // Scenario 1: Generic apply_to specialized at multiple types
    ( {
     r1 := gc_apply_to(42, (x: Int) { x + 1 })
     error += if r1 == 43 { 0 } else { 1 }

        r2 := gc_apply_to(true, (b: Bool) { if b { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }

        p  := GCPair(a = 10, b = 20)
        r3 := gc_apply_to(p, (pp: GCPair) { pp.a + pp.b })
        error += if r3 == 30 { 0 } else { 1 }
        void
    })

    // Scenario 2: Closure capturing a generic value, passed to generic function
    ( {
     val    := 100
     f      := () { val }
     result := gc_run_thunk(f)
     error += if result == 100 { 0 } else { 1 }

        bval     := true
        g        := () { bval }
        result_b := gc_run_thunk(g)
        error += if result_b == true { 0 } else { 1 }
        void
    })

    // Scenario 3: Nested generic closures - closure calls another generic fn
    ( {
     x      := 77
     f      := (v: Int) { gc_identity(v) + 1 }
     result := gc_apply_to(x, f)
     error += if result == 78 { 0 } else { 1 }

        p       := GCPair(a = 3, b = 4)
        g       := (pp: GCPair) { gc_identity(pp.a) + gc_identity(pp.b) }
        result2 := gc_apply_to(p, g)
        error += if result2 == 7 { 0 } else { 1 }
        void
    })

    // Scenario 4: Shadowed variable names inside closures with generic context
    ( {
     x  := 10
     f  := () { x }
     x  := 20
     g  := () { x }
     r1 := gc_run_thunk(f)
     r2 := gc_run_thunk(g)
     error += if r1 == 10 { 0 } else { 1 }
     error += if r2 == 20 { 0 } else { 1 }

        y       := 100
        result  := gc_apply_to(5, (v: Int) { v + y })
        y       := 200
        result2 := gc_apply_to(5, (v: Int) { v + y })
        error += if result == 105 { 0 } else { 1 }
        error += if result2 == 205 { 0 } else { 1 }
        void
    })

    // Scenario 5: Closure that mutates a captured variable, used with generics
    ( {
     counter := 0
     inc     := (n: Int) {
     counter = counter + n
     counter
     }
     r1 := gc_apply_to(10, inc)
     r2 := gc_apply_to(20, inc)
     r3 := gc_apply_to(30, inc)
     error += if r3 == 60 { 0 } else { 1 }
     error += if counter == 60 { 0 } else { 1 }
     void
     })

    // Scenario 6: combine - two closures on same generic value
    ( {
     p      := GCPair(a = 3, b = 7)
     result := gc_combine(p, (pp: GCPair) { pp.a }, (pp: GCPair) { pp.b })
     error += if result == 10 { 0 } else { 1 }

        result2 := gc_combine(42, (x: Int) { x + 1 }, (x: Int) { x * 2 })
        error += if result2 == 127 { 0 } else { 1 }
        void
    })

    // Scenario 7: map_pair with two type params
    ( {
     r1 := gc_map_pair(42, (x: Int) { x > 0 })
     error += if r1 == true { 0 } else { 1 }

        r2 := gc_map_pair(true, (b: Bool) { if b { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }
        void
    })

    error
}

// ============================================================
// From: test_stress_generic_closure_adversarial
// ============================================================

GCAPair:   { a: Int, b: Int }
GCABox[T]: { val: T         }

gca_pair_sum(p: GCAPair) -> Int { p.a + p.b }

gca_apply1[T](val: T, f: (T) -> Int) -> Int { f(val) }

gca_apply2[A, B](val: A, f: (A) -> B, g: (B) -> Int) -> Int { g(f(val)) }

gca_reduce[T](a: T, b: T, f: (T, T) -> T) -> T { f(a, b) }

gca_identity[T](x: T) -> T { x }

gca_transform[T](val: T, f: (T) -> T) -> T { f(val) }

test_generic_closure_adversarial() {
    error := 0

    // Test 1: Deeply nested generic function calls with closures (3+ levels)
    ( {
     x     := 10
     outer := (v: Int) {
     y     := v + x
     inner := (w: Int) { gca_identity(w) + 1 }
     gca_apply1(y, inner)
     }
     result := gca_apply1(5, outer)
     error += if result == 16 { 0 } else { 1 }
     void
     })

    // Test 2: Same test with GCAPair type (different specialization)
    ( {
     offset := 100
     f      := (p: GCAPair) {
     inner  := (q: GCAPair) { q.a + q.b + offset }
     gca_apply1(p, inner)
     }
     p      := GCAPair(a = 3, b = 7)
     result := gca_apply1(p, f)
     error += if result == 110 { 0 } else { 1 }
     void
     })

    // Test 3: Generic reduce specialized at multiple types
    ( {
     r1 := gca_reduce(10, 20, (a: Int, b: Int) { a + b })
     error += if r1 == 30 { 0 } else { 1 }

        r2 := gca_reduce(true, false, (a: Bool, b: Bool) { a || b })
        error += if r2 == true { 0 } else { 1 }

        r3 := gca_reduce(false, false, (a: Bool, b: Bool) { a || b })
        error += if r3 == false { 0 } else { 1 }

        p1 := GCAPair(a = 1, b = 2)
        p2 := GCAPair(a = 3, b = 4)
        r4 := gca_reduce(p1, p2, (a: GCAPair, b: GCAPair) { GCAPair(a = a.a + b.a, b = a.b + b.b) })
        error += if r4.a == 4 { 0 } else { 1 }
        error += if r4.b == 6 { 0 } else { 1 }
        void
    })

    // Test 4: Multiple closures from same generic function, different specializations
    ( {
     v1 := 42
     v2 := true
     v3 := GCAPair(a = 5, b = 6)

        f1 := (x: Int) { x + 1 }
        f2 := (b: Bool) { if b { 10 } else { 20 } }
        f3 := (p: GCAPair) { p.a * p.b }

        r1 := gca_apply1(v1, f1)
        r2 := gca_apply1(v2, f2)
        r3 := gca_apply1(v3, f3)

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

        r1 := gca_apply1(1, fa)
        r2 := gca_apply1(1, fb)
        r3 := gca_apply1(1, fc)

        error += if r1 == 101 { 0 } else { 1 }
        error += if r2 == 201 { 0 } else { 1 }
        error += if r3 == 301 { 0 } else { 1 }
        void
    })

    // Test 6: Generic closure stored in a generic struct field
    ( {
     b1 := GCABox(val = 42)
     r1 := gca_apply1(b1, (bx: GCABox[Int]) { bx.val + 1 })
     error += if r1 == 43 { 0 } else { 1 }

        b2 := GCABox(val = true)
        r2 := gca_apply1(b2, (bx: GCABox[Bool]) { if bx.val { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }

        b3 := GCABox(val = GCABox(val = 99))
        r3 := gca_apply1(b3, (bx: GCABox[GCABox[Int]]) { bx.val.val })
        error += if r3 == 99 { 0 } else { 1 }
        void
    })

    // Test 7: apply2 - two-step generic pipeline with closures
    ( {
     r1 := gca_apply2(42, (x: Int) { x > 0 }, (b: Bool) { if b { 1 } else { 0 } })
     error += if r1 == 1 { 0 } else { 1 }

        p  := GCAPair(a = 3, b = 4)
        r2 := gca_apply2(p, (pp: GCAPair) { pp.a + pp.b }, (x: Int) { x * 2 })
        error += if r2 == 14 { 0 } else { 1 }

        r3 := gca_apply2(false, (b: Bool) { if b { 10 } else { 20 } }, (x: Int) { x + 5 })
        error += if r3 == 25 { 0 } else { 1 }
        void
    })

    // Test 8: transform chain - same generic fn applied repeatedly
    ( {
     add1 := (x: Int) { x + 1 }
     r    := gca_transform(gca_transform(gca_transform(0, add1), add1), add1)
     error += if r == 3 { 0 } else { 1 }

        shift := (p: GCAPair) { GCAPair(a = p.a + 1, b = p.b + 1) }
        p     := GCAPair(a = 0, b = 0)
        rp    := gca_transform(gca_transform(p, shift), shift)
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
     r := gca_reduce(10, 20, accumulate)
     error += if r == 30 { 0 } else { 1 }
     error += if sum == 30 { 0 } else { 1 }

        r2 := gca_reduce(3, 7, accumulate)
        error += if r2 == 10 { 0 } else { 1 }
        error += if sum == 40 { 0 } else { 1 }
        void
    })

    // Test 10: Deeply nested - 3 levels of generic fn + closure interleaving
    ( {
     base   := 1000
     result := gca_apply1(5, (a: Int) {
     gca_apply1(a + 10, (b: Int) {
     gca_apply1(b + 100, (c: Int) {
     c + base
     })
     })
     })
     error += if result == 1115 { 0 } else { 1 }
     void
     })

    error
}

// ============================================================
// From: test_stress_recursive_generic_closures
// ============================================================

RGCList[T]: | RGCCons { head: T, tail: Ptr[RGCList[T]] }
            | RGCNil

rgc_foreach[T](list: RGCList[T], f: (T) -> Int) -> Int {
    when list {
        c: RGCCons {
            f(c.head)
            next := c.tail.*
            rgc_foreach(next, f)
        }
        n: RGCNil { 0 }
    }
}

rgc_sum_list(list: RGCList[Int]) -> Int {
    when list {
        c: RGCCons {
            next := c.tail.*
            c.head + rgc_sum_list(next)
        }
        n: RGCNil { 0 }
    }
}

rgc_length_list(list: RGCList[Int]) -> Int {
    when list {
        c: RGCCons {
            next := c.tail.*
            1 + rgc_length_list(next)
        }
        n: RGCNil { 0 }
    }
}

rgc_test_foreach_closure() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(30, nil.&)
    n2                := RGCCons(20, n3.&)
    n1                := RGCCons(10, n2.&)

    total        := 0
    add_to_total := (x: Int) {
        total = total + x
        0
    }
    rgc_foreach(n1, add_to_total)

    if total == 60 {0} else {1}
}

rgc_test_sum_list() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(3, nil.&)
    n2                := RGCCons(2, n3.&)
    n1                := RGCCons(1, n2.&)

    result := rgc_sum_list(n1)
    if result == 6 {0} else {1}
}

rgc_test_closure_captures_list_ptr() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(15, nil.&)
    n2                := RGCCons(10, n3.&)
    n1                := RGCCons(5, n2.&)

    get_sum := () { rgc_sum_list(n1) }

    result := get_sum()
    if result == 30 {0} else {1}
}

rgc_test_length_with_closure() {
    nil: RGCList[Int] := RGCNil()
    n4                := RGCCons(40, nil.&)
    n3                := RGCCons(30, n4.&)
    n2                := RGCCons(20, n3.&)
    n1                := RGCCons(10, n2.&)

    get_len := () { rgc_length_list(n1) }

    if get_len() == 4 {0} else {1}
}

rgc_test_foreach_multiply() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(3, nil.&)
    n2                := RGCCons(2, n3.&)
    n1                := RGCCons(1, n2.&)

    total      := 0
    factor     := 10
    add_scaled := (x: Int) {
        total = total + x * factor
        0
    }
    rgc_foreach(n1, add_scaled)

    if total == 60 {0} else {1}
}

rgc_test_closure_sees_mutations() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(3, nil.&)
    n2                := RGCCons(2, n3.&)
    n1                := RGCCons(1, n2.&)

    offset  := 0
    compute := () { rgc_sum_list(n1) + offset }

    r1 := compute()
    offset = 100
    r2 := compute()

    error := 0
    error += if r1 == 6 {0} else {1}
    error += if r2 == 106 {0} else {1}
    error
}

rgc_test_multiple_closures_same_list() {
    nil: RGCList[Int] := RGCNil()
    n3                := RGCCons(3, nil.&)
    n2                := RGCCons(2, n3.&)
    n1                := RGCCons(1, n2.&)

    get_sum    := () { rgc_sum_list(n1) }
    get_length := () { rgc_length_list(n1) }

    error := 0
    error += if get_sum() == 6 {0} else {1}
    error += if get_length() == 3 {0} else {1}
    error
}

test_recursive_generic_closures() {
    error := 0
    error += rgc_test_foreach_closure()
    error += rgc_test_sum_list()
    error += rgc_test_closure_captures_list_ptr()
    error += rgc_test_length_with_closure()
    error += rgc_test_foreach_multiply()
    error += rgc_test_closure_sees_mutations()
    error += rgc_test_multiple_closures_same_list()
    error
}

// ============================================================
// From: test_stress_recursive_generic_closures_adversarial
// ============================================================

Tree[T]: | Leaf { v: T }
         | Node { v: T, left: Ptr[Tree[T]], right: Ptr[Tree[T]] }

tree_fold[T](tree: Tree[T], f_leaf: (T) -> Int, f_node: (T, Int, Int) -> Int) -> Int {
    when tree {
        l: Leaf { f_leaf(l.v) }
        n: Node {
            left_val  := tree_fold(n.left.*, f_leaf, f_node)
            right_val := tree_fold(n.right.*, f_leaf, f_node)
            f_node(n.v, left_val, right_val)
        }
    }
}

tree_sum(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { l.v }
        n: Node {
            n.v + tree_sum(n.left.*) + tree_sum(n.right.*)
        }
    }
}

tree_depth(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { 1 }
        n: Node {
            ld := tree_depth(n.left.*)
            rd := tree_depth(n.right.*)
            d  := if ld > rd { ld } else { rd }
            1 + d
        }
    }
}

tree_count(tree: Tree[Int]) -> Int {
    when tree {
        l: Leaf { 1 }
        n: Node {
            tree_count(n.left.*) + tree_count(n.right.*)
        }
    }
}

tr_test_tree_fold_sum() {
    l1   := Leaf(1)
    l2   := Leaf(2)
    root := Node(10, l1.&, l2.&)

    identity := (x: Int) { x }
    combine  := (v: Int, left_sum: Int, right_sum: Int) { v + left_sum + right_sum }
    result   := tree_fold(root, identity, combine)

    if result == 13 {0} else {1}
}

tr_test_tree_fold_count() {
    l1    := Leaf(10)
    l2    := Leaf(20)
    l3    := Leaf(30)
    l4    := Leaf(40)
    left  := Node(0, l1.&, l2.&)
    right := Node(0, l3.&, l4.&)
    root  := Node(0, left.&, right.&)

    one      := (x: Int) { 1 }
    sum_kids := (v: Int, a: Int, b: Int) { a + b }
    result   := tree_fold(root, one, sum_kids)

    if result == 4 {0} else {1}
}

tr_test_closure_captures_tree() {
    l1   := Leaf(5)
    l2   := Leaf(15)
    root := Node(0, l1.&, l2.&)

    get_sum := () { tree_sum(root) }

    if get_sum() == 20 {0} else {1}
}

tr_test_tree_fold_with_capture() {
    l1    := Leaf(1)
    l2    := Leaf(2)
    l3    := Leaf(3)
    right := Node(0, l2.&, l3.&)
    root  := Node(0, l1.&, right.&)

    scale       := 10
    scaled_leaf := (x: Int) { x * scale }
    combine     := (v: Int, a: Int, b: Int) { a + b }
    result      := tree_fold(root, scaled_leaf, combine)

    if result == 60 {0} else {1}
}

tr_test_tree_depth_closure() {
    l1   := Leaf(1)
    l2   := Leaf(2)
    l3   := Leaf(3)
    l4   := Leaf(4)
    deep := Node(0, l3.&, l4.&)
    mid  := Node(0, l2.&, deep.&)
    root := Node(0, l1.&, mid.&)

    get_depth := () { tree_depth(root) }

    if get_depth() == 4 {0} else {1}
}

tr_test_multiple_closures_same_tree() {
    l1   := Leaf(10)
    l2   := Leaf(20)
    l3   := Leaf(30)
    left := Node(0, l1.&, l2.&)
    root := Node(0, left.&, l3.&)

    get_sum   := () { tree_sum(root) }
    get_depth := () { tree_depth(root) }
    get_count := () { tree_count(root) }

    error := 0
    error += if get_sum() == 60 {0} else {1}
    error += if get_depth() == 3 {0} else {1}
    error += if get_count() == 3 {0} else {1}
    error
}

tr_test_tree_fold_max() {
    l1    := Leaf(1)
    l2    := Leaf(42)
    l3    := Leaf(3)
    l4    := Leaf(7)
    left  := Node(0, l1.&, l2.&)
    right := Node(0, l3.&, l4.&)
    root  := Node(0, left.&, right.&)

    get_val  := (x: Int) { x }
    pick_max := (v: Int, a: Int, b: Int) {
        m := if a > b { a } else { b }
        if v > m { v } else { m }
    }
    result := tree_fold(root, get_val, pick_max)

    if result == 42 {0} else {1}
}

tr_test_closure_mutable_with_tree() {
    l1   := Leaf(5)
    l2   := Leaf(10)
    root := Node(1, l1.&, l2.&)

    offset  := 0
    compute := () { tree_sum(root) + offset }

    error := 0
    error += if compute() == 16 {0} else {1}

    offset = 100
    error += if compute() == 116 {0} else {1}
    error
}

test_recursive_generic_closures_adversarial() {
    error := 0
    error += tr_test_tree_fold_sum()
    error += tr_test_tree_fold_count()
    error += tr_test_closure_captures_tree()
    error += tr_test_tree_fold_with_capture()
    error += tr_test_tree_depth_closure()
    error += tr_test_multiple_closures_same_tree()
    error += tr_test_tree_fold_max()
    error += tr_test_closure_mutable_with_tree()
    error
}

// ============================================================
// Main
// ============================================================

main() {
    error: CInt := 0
    error += test_alloc_closures()
    error += test_closures()
    error += test_generic_closures()
    error += test_generic_closure_adversarial()
    error += test_recursive_generic_closures()
    error += test_recursive_generic_closures_adversarial()
    error
}
