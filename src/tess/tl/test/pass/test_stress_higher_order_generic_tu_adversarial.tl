#module main
#import <cstdlib.tl>

// --- Linked list as tagged union (recursive via Ptr) ---
MyList[T]: | Cons { head: T, tail: Ptr[MyList[T]] }
           | LNil

// --- Result type (single type param, error is always Int) ---
MyRes[T]: | MyOk  { v: T }
          | MyErr { code: Int }

// --- Option-like ---
Opt[T]: | Val { v: T }
        | Nope

// ============================================================
// Helpers for linked list
// ============================================================
make_nil[T]() -> Ptr[MyList[T]] {
    p: Ptr[MyList[T]] := c_malloc(sizeof[MyList[T]]())
    p.* = LNil()
    p
}

make_cons[T](head: T, tail: Ptr[MyList[T]]) -> Ptr[MyList[T]] {
    p: Ptr[MyList[T]] := c_malloc(sizeof[MyList[T]]())
    p.* = Cons(head, tail)
    p
}

// ============================================================
// fold over a linked list with a function pointer accumulator
// ============================================================
fold[T, U](lst: Ptr[MyList[T]], acc: U, f) -> U {
    node := lst.*
    when node {
        c: Cons { fold(c.tail, f(acc, c.head), f) }
        n: LNil { acc }
    }
}

// ============================================================
// map from one TU type to a DIFFERENT TU type
// MyList[Int] -> MyRes[Int] by folding
// ============================================================
list_sum_to_res(lst: Ptr[MyList[Int]]) -> MyRes[Int] {
    total := fold(lst, 0, (acc: Int, x: Int) { acc + x })
    if total > 0 { MyOk(total) } else { MyErr(0 - total) }
}

// ============================================================
// Higher-order generic function specialized at 3+ type combos
// ============================================================
apply_or_default[T](o: Opt[T], f, def: T) -> T {
    when o {
        s: Val { f(s.v) }
        n: Nope { def }
    }
}

double_int(x: Int)         -> Int         { x * 2 }
add_ints  (a: Int, b: Int) -> Int { a + b         }
not_bool  (b: Bool)        -> Bool       { !b     }

// ============================================================
// unwrap helpers
// ============================================================
opt_unwrap[T](o: Opt[T], def: T) -> T {
    when o {
        s: Val { s.v }
        n: Nope { def }
    }
}

res_unwrap[T](r: MyRes[T], def: T) -> T {
    when r {
        o: MyOk { o.v }
        e: MyErr { def }
    }
}

// ============================================================
// map Opt to MyRes through a generic function
// ============================================================
opt_to_res[T](o: Opt[T], err_code: Int) -> MyRes[T] {
    when o {
        s: Val { MyOk(s.v) }
        n: Nope { MyErr(err_code) }
    }
}

// ============================================================
// Nested tagged unions: Opt[MyRes[T]]
// ============================================================
flatten_opt_res[T](o: Opt[MyRes[T]], def: T) -> T {
    when o {
        s: Val {
            when s.v {
                ok: MyOk { ok.v }
                er: MyErr { def }
            }
        }
        n: Nope { def }
    }
}

// ============================================================
// Higher-order: process nested Opt[MyRes[Int]] with a function
// ============================================================
process_nested(o: Opt[MyRes[Int]], f, def: Int) -> Int {
    inner := flatten_opt_res(o, def)
    f(inner)
}

main() {
    error := 0

    // --- Test 1: fold over a list to sum ---
    lst1 := make_cons(1, make_cons(2, make_cons(3, make_nil[Int]())))
    sum1 := fold(lst1, 0, (acc: Int, x: Int) { acc + x })
    error += if sum1 == 6 {0} else {1}

    // --- Test 2: fold with multiplication ---
    lst2 := make_cons(2, make_cons(3, make_cons(4, make_nil[Int]())))
    prod := fold(lst2, 1, (acc: Int, x: Int) { acc * x })
    error += if prod == 24 {0} else {1}

    // --- Test 3: fold with function pointer (not lambda) ---
    lst3 := make_cons(10, make_cons(20, make_cons(30, make_nil[Int]())))
    sum3 := fold(lst3, 0, add_ints/2)
    error += if sum3 == 60 {0} else {1}

    // --- Test 4: map List -> MyRes (cross-TU-type) ---
    lst4 := make_cons(5, make_cons(10, make_cons(15, make_nil[Int]())))
    r4   := list_sum_to_res(lst4)
    error += if res_unwrap(r4, 0) == 30 {0} else {1}

    // --- Test 5: cross-TU negative case ---
    lst5 := make_cons(-5, make_cons(-10, make_nil[Int]()))
    r5   := list_sum_to_res(lst5)
    // sum = -15, so MyErr(15)
    err_code := when r5 {
        o: MyOk { 0 }
        e: MyErr { e.code }
    }
    error += if err_code == 15 {0} else {1}

    // --- Test 6: apply_or_default with Int ---
    o6 := Val(21)
    r6 := apply_or_default(o6, double_int/1, 0)
    error += if r6 == 42 {0} else {1}

    // --- Test 7: apply_or_default with Int, Nope case ---
    n7: Opt[Int] := Nope()
    r7           := apply_or_default(n7, double_int/1, -1)
    error += if r7 == -1 {0} else {1}

    // --- Test 8: apply_or_default with Bool (different specialization) ---
    o8 := Val(true)
    r8 := apply_or_default(o8, not_bool/1, false)
    error += if r8 == false {0} else {1}

    // --- Test 9: opt_to_res: Opt -> MyRes cross-type mapping ---
    o9 := Val(99)
    r9 := opt_to_res(o9, -1)
    error += if res_unwrap(r9, 0) == 99 {0} else {1}

    // --- Test 10: opt_to_res Nope case ---
    n10: Opt[Int] := Nope()
    r10           := opt_to_res(n10, 42)
    err10         := when r10 {
        o: MyOk { 0 }
        e: MyErr { e.code }
    }
    error += if err10 == 42 {0} else {1}

    // --- Test 11: nested TU: Opt[MyRes[Int]] ---
    nested1 := Val(MyOk(77))
    r11     := flatten_opt_res(nested1, 0)
    error += if r11 == 77 {0} else {1}

    // --- Test 12: nested TU: Opt[MyRes[Int]] with MyErr inside ---
    r12: MyRes[Int] := MyErr(5)
    nested2         := Val(r12)
    r12b            := flatten_opt_res(nested2, -1)
    error += if r12b == -1 {0} else {1}

    // --- Test 13: nested TU: Nope at outer level ---
    nested3: Opt[MyRes[Int]] := Nope()
    r13                      := flatten_opt_res(nested3, -2)
    error += if r13 == -2 {0} else {1}

    // --- Test 14: process_nested with function pointer ---
    nested4 := Val(MyOk(10))
    r14     := process_nested(nested4, double_int/1, 0)
    error += if r14 == 20 {0} else {1}

    // --- Test 15: closure capturing a tagged union, passed to generic ---
    captured_opt := Val(100)
    closure      := () { opt_unwrap(captured_opt, 0) }
    r15          := closure()
    error += if r15 == 100 {0} else {1}

    // --- Test 16: apply_or_default with lambda (3rd specialization: Int with lambda) ---
    o16 := Val(5)
    r16 := apply_or_default(o16, (x: Int) { x * x * x }, 0)
    error += if r16 == 125 {0} else {1}

    error
}
