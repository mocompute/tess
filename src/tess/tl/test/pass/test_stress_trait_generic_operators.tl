// Stress test: trait-bounded generics with operator overloading.
// Exercises operator rewriting during specialization + trait bound verification.

// --- Module A: a 2D vector type with Add, Eq, Ord ---
#module VecA

Vec2 : { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
eq(a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y }
cmp(a: Vec2, b: Vec2) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

// --- Module B: a wrapped-int type with Add, Eq, Ord ---
#module NumB

Num : { val: Int }

add(a: Num, b: Num) -> Num { Num(val = a.val + b.val) }
eq(a: Num, b: Num) -> Bool { a.val == b.val }
cmp(a: Num, b: Num) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}

// --- Module C: a 3-field type with Add, Eq, Ord ---
#module TriC

Tri : { a: Int, b: Int, c: Int }

add(x: Tri, y: Tri) -> Tri { Tri(a = x.a + y.a, b = x.b + y.b, c = x.c + y.c) }
eq(x: Tri, y: Tri) -> Bool { x.a == y.a && x.b == y.b && x.c == y.c }
cmp(x: Tri, y: Tri) -> CInt {
    if x.a < y.a { -1 }
    else if x.a > y.a { 1 }
    else if x.b < y.b { -1 }
    else if x.b > y.b { 1 }
    else if x.c < y.c { -1 }
    else if x.c > y.c { 1 }
    else { 0 }
}

// --- Module Wrap: a tagged union that wraps a value ---
#module Wrap

Wrapped[a]: | Has { v: a } | Empty

// --- Main module ---
#module main

// Combined trait: Add + Eq
AddEq[T] : Add[T], Eq[T] { }

// Combined trait: Add + Eq + Ord (Ord inherits Eq, but explicit for coverage)
AddOrd[T] : Add[T], Ord[T] { }

// 1. Simple trait-bounded generic: Add
double[T: Add](x: T) -> T { x + x }

// 2. Simple trait-bounded generic: Eq
are_equal[T: Eq](a: T, b: T) -> Bool { a == b }

// 3. Simple trait-bounded generic: Ord
is_less[T: Ord](a: T, b: T) -> Bool { a < b }

// 4. Chained operators in generic context: a + b + c
triple_add[T: Add](a: T, b: T, c: T) -> T { a + b + c }

// 5. Combined trait bound: uses both + and ==
add_if_different[T: AddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

// 6. Combined trait bound with Ord: uses +, <, ==
sum_if_ordered[T: AddOrd](a: T, b: T) -> T {
    if a < b { a + b }
    else if a == b { a }
    else { b + a }
}

// 7. Nested generic calls: outer calls inner, both trait-bounded
double_then_compare[T: AddEq](a: T, b: T) -> Bool {
    da := double(a)
    db := double(b)
    are_equal(da, db)
}

// 8. Generic function with tagged union result using trait-bounded ops
wrap_sum[T: Add](a: T, b: T) -> Wrap.Wrapped[T] {
    Wrap.Wrapped.Has(v = a + b)
}

main() {
    error := 0

    // --- Test with VecA.Vec2 ---
    va1 := VecA.Vec2(x = 1, y = 2)
    va2 := VecA.Vec2(x = 3, y = 4)
    va3 := VecA.Vec2(x = 5, y = 6)

    // double
    d_va := double(va1)
    error += if d_va.x == 2 && d_va.y == 4 { 0 } else { 1 }

    // are_equal
    error += if are_equal(va1, va1) { 0 } else { 1 }
    error += if are_equal(va1, va2) { 1 } else { 0 }

    // is_less
    error += if is_less(va1, va2) { 0 } else { 1 }
    error += if is_less(va2, va1) { 1 } else { 0 }

    // triple_add (chained operators)
    t_va := triple_add(va1, va2, va3)
    error += if t_va.x == 9 && t_va.y == 12 { 0 } else { 1 }

    // add_if_different (combined bound)
    r_diff := add_if_different(va1, va2)
    error += if r_diff.x == 4 && r_diff.y == 6 { 0 } else { 1 }
    r_same := add_if_different(va1, va1)
    error += if r_same.x == 1 && r_same.y == 2 { 0 } else { 1 }

    // sum_if_ordered
    r_ord := sum_if_ordered(va1, va2)
    error += if r_ord.x == 4 && r_ord.y == 6 { 0 } else { 1 }

    // double_then_compare (nested generic calls)
    error += if double_then_compare(va1, va1) { 0 } else { 1 }
    error += if double_then_compare(va1, va2) { 1 } else { 0 }

    // wrap_sum (tagged union + trait bound)
    ws_va := wrap_sum(va1, va2)
    sv: Wrap.Has := ws_va else { return 99 }
    error += if sv.v.x == 4 && sv.v.y == 6 { 0 } else { 1 }

    // --- Test with NumB.Num ---
    nb1 := NumB.Num(val = 10)
    nb2 := NumB.Num(val = 20)
    nb3 := NumB.Num(val = 30)

    // double
    d_nb := double(nb1)
    error += if d_nb.val == 20 { 0 } else { 1 }

    // are_equal
    error += if are_equal(nb1, nb1) { 0 } else { 1 }
    error += if are_equal(nb1, nb2) { 1 } else { 0 }

    // is_less
    error += if is_less(nb1, nb2) { 0 } else { 1 }

    // triple_add
    t_nb := triple_add(nb1, nb2, nb3)
    error += if t_nb.val == 60 { 0 } else { 1 }

    // add_if_different
    r_nb := add_if_different(nb1, nb2)
    error += if r_nb.val == 30 { 0 } else { 1 }

    // sum_if_ordered
    r_nb_ord := sum_if_ordered(nb1, nb2)
    error += if r_nb_ord.val == 30 { 0 } else { 1 }

    // double_then_compare (nested)
    error += if double_then_compare(nb1, nb1) { 0 } else { 1 }

    // wrap_sum
    ws_nb := wrap_sum(nb1, nb2)
    snb: Wrap.Has := ws_nb else { return 99 }
    error += if snb.v.val == 30 { 0 } else { 1 }

    // --- Test with TriC.Tri ---
    tc1 := TriC.Tri(a = 1, b = 2, c = 3)
    tc2 := TriC.Tri(a = 4, b = 5, c = 6)

    // double
    d_tc := double(tc1)
    error += if d_tc.a == 2 && d_tc.b == 4 && d_tc.c == 6 { 0 } else { 1 }

    // triple_add
    tc3 := TriC.Tri(a = 7, b = 8, c = 9)
    t_tc := triple_add(tc1, tc2, tc3)
    error += if t_tc.a == 12 && t_tc.b == 15 && t_tc.c == 18 { 0 } else { 1 }

    // add_if_different
    r_tc := add_if_different(tc1, tc2)
    error += if r_tc.a == 5 && r_tc.b == 7 && r_tc.c == 9 { 0 } else { 1 }

    error
}
