// Combined stress test for miscellaneous features.
// Merged from: test_stress_control_flow, test_stress_deep_nesting,
// test_stress_expression_position, test_stress_scope_shadow,
// test_stress_trait_generic_operators, test_stress_trait_generic_operators_adversarial,
// test_stress_type_features, test_stress_when_combinations.

// ============================================================
// From: test_stress_control_flow (modules: MyOption, main)
// ============================================================

#module CFOption

T[a]: | MySome { value: a }
      | MyNone

// ============================================================
// From: test_stress_trait_generic_operators (modules: VecA, NumB, TriC, Wrap, main)
// ============================================================

#module VecA

Vec2: { x: Int, y: Int }

add(a: Vec2, b: Vec2) -> Vec2 { Vec2(x = a.x + b.x, y = a.y + b.y) }
eq (a: Vec2, b: Vec2) -> Bool { a.x == b.x && a.y == b.y           }

cmp(a: Vec2, b: Vec2) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

#module NumB

Num: { val: Int }

add(a: Num, b: Num) -> Num  { Num(val = a.val + b.val) }
eq (a: Num, b: Num) -> Bool { a.val == b.val           }

cmp(a: Num, b: Num) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}

#module TriC

Tri: { a: Int, b: Int, c: Int }

add(x: Tri, y: Tri) -> Tri  { Tri(a = x.a + y.a, b = x.b + y.b, c = x.c + y.c) }
eq (x: Tri, y: Tri) -> Bool { x.a == y.a && x.b == y.b && x.c == y.c           }

cmp(x: Tri, y: Tri) -> CInt {
    if x.a < y.a { -1 }
    else if x.a > y.a { 1 }
    else if x.b < y.b { -1 }
    else if x.b > y.b { 1 }
    else if x.c < y.c { -1 }
    else if x.c > y.c { 1 }
    else { 0 }
}

#module TGOWrap

Wrapped[a]: | Has { v: a } | Empty

// ============================================================
// From: test_stress_trait_generic_operators_adversarial
// (modules: Pair, Weight, Score, main)
// ============================================================

#module TGOAPair

T: { x: Int, y: Int }

add(a: T, b: T) -> T    { T(x = a.x + b.x, y = a.y + b.y) }
eq (a: T, b: T) -> Bool { a.x == b.x && a.y == b.y        }

cmp(a: T, b: T) -> CInt {
    if a.x < b.x { -1 }
    else if a.x > b.x { 1 }
    else if a.y < b.y { -1 }
    else if a.y > b.y { 1 }
    else { 0 }
}

#module Weight

T: { grams: Int }

add(a: T, b: T) -> T    { T(grams = a.grams + b.grams) }
eq (a: T, b: T) -> Bool { a.grams == b.grams           }

cmp(a: T, b: T) -> CInt {
    if a.grams < b.grams { -1 }
    else if a.grams > b.grams { 1 }
    else { 0 }
}

#module Score

T: { points: Int }

add(a: T, b: T) -> T    { T(points = a.points + b.points) }
sub(a: T, b: T) -> T    { T(points = a.points - b.points) }
eq (a: T, b: T) -> Bool { a.points == b.points            }

cmp(a: T, b: T) -> CInt {
    if a.points < b.points { -1 }
    else if a.points > b.points { 1 }
    else { 0 }
}

// ============================================================
// Main module
// ============================================================

#module main

// ============================================================
// From: test_stress_control_flow
// ============================================================

CFShape: | CFCircle { radius: Int }
         | CFSquare { length: Int }
         | CFNone

cf_test_break_from_when() {
    shapes: CArray[CFShape, 4] := void
    shapes.[0] = CFCircle(1)
    shapes.[1] = CFSquare(2)
    shapes.[2] = CFCircle(10)
    shapes.[3] = CFSquare(4)

    found             := 0
    i                 := 0
    ptr: Ptr[CFShape] := shapes
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        when elem {
            c: CFCircle {
                if c.radius >= 10 {
                    found = c.radius
                    break
                }
            }
            else { }
        }
    }
    if found == 10 {0} else {1}
}

cf_test_continue_from_when() {
    shapes: CArray[CFShape, 4] := void
    shapes.[0] = CFCircle(1)
    shapes.[1] = CFNone()
    shapes.[2] = CFCircle(3)
    shapes.[3] = CFNone()

    sum               := 0
    i                 := 0
    ptr: Ptr[CFShape] := shapes
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        when elem {
            n: CFNone { continue }
            else { }
        }
        sum = sum + when elem {
            c:  CFCircle { c.radius }
            sq: CFSquare { sq.length }
            else { 0 }
        }
    }
    if sum == 4 {0} else {1}
}

cf_deep_return(s: CFShape, flag: Int) -> Int {
    when s {
        c: CFCircle {
            if flag > 0 {
                if c.radius > 5 {
                    return 999
                }
                c.radius
            } else {
                0
            }
        }
        sq: CFSquare { sq.length }
        n:  CFNone { -1 }
    }
}

cf_test_deep_return() {
    error := 0
    error += if cf_deep_return(CFCircle(10), 1) == 999 {0} else {1}
    error += if cf_deep_return(CFCircle(3), 1) == 3 {0} else {1}
    error += if cf_deep_return(CFCircle(10), 0) == 0 {0} else {1}
    error
}

cf_count_down(s: CFShape) -> Int {
    when s {
        c: CFCircle {
            if c.radius <= 0 { 0 }
            else { cf_count_down(CFCircle(c.radius - 1)) }
        }
        else { -1 }
    }
}

cf_test_tail_call_from_when() {
    result := cf_count_down(CFCircle(100))
    if result == 0 {0} else {1}
}

cf_test_bail_in_loop_break() {
    result := 0
    i      := 0
    while i < 5, i = i + 1 {
        val       := if i == 3 { CFOption.MySome(42) } else { CFOption.MyNone() }
        s: MySome := val else { continue }
        result = s.value
        break
    }
    if result == 42 {0} else {1}
}

cf_test_bail_with_return() -> Int {
    val       := CFOption.MySome(77)
    s: MySome := val else { return 1 }
    if s.value == 77 {0} else {1}
}

test_control_flow() {
    error := 0
    error += cf_test_break_from_when()
    error += cf_test_continue_from_when()
    error += cf_test_deep_return()
    error += cf_test_tail_call_from_when()
    error += cf_test_bail_in_loop_break()
    error += cf_test_bail_with_return()
    result: CInt := error
    result
}

// ============================================================
// From: test_stress_deep_nesting
// ============================================================

DNInner: | DNIA { value: Int }
         | DNIB

DNMiddle: | DNMA { inner: DNInner }
          | DNMB

DNOuter: | DNOA { inner: DNMiddle }
         | DNOB

dn_test_nested_when() {
    x := DNOA(DNMA(DNIA(42)))

    result := when x {
        oa: DNOA {
            when oa.inner {
                ma: DNMA {
                    when ma.inner {
                        ia: DNIA { ia.value }
                        ib: DNIB { -1 }
                    }
                }
                mb: DNMB { -2 }
            }
        }
        ob: DNOB { -3 }
    }
    if result == 42 {0} else {1}
}

DNColor: | DNRed | DNGreen | DNBlue

dn_test_nested_when_different_types() {
    x := DNOA(DNMA(DNIA(10)))
    c := DNGreen()

    result := when x {
        oa: DNOA {
            when oa.inner {
                ma: DNMA {
                    val := when ma.inner {
                        ia: DNIA { ia.value }
                        ib: DNIB { 0 }
                    }
                    when c {
                        r: DNRed { val + 1 }
                        g: DNGreen { val + 2 }
                        b: DNBlue { val + 3 }
                    }
                }
                mb: DNMB { 0 }
            }
        }
        ob: DNOB { 0 }
    }
    if result == 12 {0} else {1}
}

dn_test_deep_if_else() {
    a := 1
    b := 2
    c := 3
    d := 4
    e := 5
    f := 6

    result := if a == 1 {
        if b == 2 {
            if c == 3 {
                if d == 4 {
                    if e == 5 {
                        if f == 6 {
                            100
                        } else { 0 }
                    } else { 0 }
                } else { 0 }
            } else { 0 }
        } else { 0 }
    } else { 0 }

    if result == 100 {0} else {1}
}

dn_test_nested_blocks() {
    result := ( {
               a := ( {
               b := ( {
               c := ( {
               10 + 20
               })
               c + 5
               })
               b * 2
               })
               a - 70
               })
    if result == 0 {0} else {1}
}

dn_test_nested_let_in() {
    result := (a := 1
               (b := a + 1
               (c := b + 1
               (d := c + 1
               (e := d + 1
               a + b + c + d + e)))))
    if result == 15 {0} else {1}
}

dn_test_mixed_deep_nesting() {
    x := DNOA(DNMA(DNIA(7)))

    result := when x {
        oa: DNOA {
            if 1 == 1 {
                ( {
                 when oa.inner {
                 ma: DNMA {
                 if 2 == 2 {
                 ( {
                 when ma.inner {
                 ia: DNIA { ia.value * 3 }
                 ib: DNIB { 0 }
                 }
                 })
                 } else { 0 }
                 }
                 mb: DNMB { 0 }
                 }
                 })
            } else { 0 }
        }
        ob: DNOB { 0 }
    }
    if result == 21 {0} else {1}
}

test_deep_nesting() {
    error := 0
    error += dn_test_nested_when()
    error += dn_test_nested_when_different_types()
    error += dn_test_deep_if_else()
    error += dn_test_nested_blocks()
    error += dn_test_nested_let_in()
    error += dn_test_mixed_deep_nesting()
    error
}

// ============================================================
// From: test_stress_expression_position
// ============================================================

EPPoint: { x: Int, y: Int }

ep_test_block_as_field_init() {
    p := EPPoint(
               x = ( { 1 + 2 }),
               y = ( { 3 + 4 })
               )
    error := 0
    error += if p.x == 3 {0} else {1}
    error += if p.y == 7 {0} else {1}
    error
}

ep_add(a: Int, b: Int) -> Int { a + b }

ep_test_if_as_argument() {
    result := ep_add(
                  if 1 == 1 { 10 } else { 0 },
                  if 2 == 2 { 20 } else { 0 }
                  )
    if result == 30 {0} else {1}
}

ep_test_nested_if_both_branches() {
    a := 1
    b := 2

    result := if a == 1 {
        if b == 2 { 100 } else { 50 }
    } else {
        if b == 2 { 25 } else { 0 }
    }
    if result == 100 {0} else {1}
}

ep_test_block_in_arithmetic() {
    result := ( {
               a := 10
               a
               }) + ( {
               b := 20
               b
               }) + ( {
               c := 30
               c
               })
    if result == 60 {0} else {1}
}

ep_test_let_in_as_loop_bound() {
    limit := (x := 3
              y := x + 2
              y)
    sum := 0
    i   := 0
    while i < limit, i = i + 1 {
        sum = sum + i
    }
    if sum == 10 {0} else {1}
}

ep_test_deeply_nested_expression() {
    result := 1 + ( {
                   x := if 1 == 1 { 10 } else { 0 }
                   y := if 2 == 2 { 20 } else { 0 }
                   x + y
                   })
    if result == 31 {0} else {1}
}

EPShape: | EPCircle { radius: Int }
         | EPSquare { length: Int }

ep_test_when_then_case() {
    s   := EPCircle(2)
    val := when s {
        c:  EPCircle { c.radius }
        sq: EPSquare { sq.length }
    }
    result := case val {
        1 { 10 }
        2 { 20 }
        3 { 30 }
        else { 0 }
    }
    if result == 20 {0} else {1}
}

ep_test_block_as_index() {
    arr: CArray[Int, 5] := void
    ptr: Ptr[Int]       := arr
    ptr.[0] = 10
    ptr.[1] = 20
    ptr.[2] = 30
    ptr.[3] = 40
    ptr.[4] = 50

    idx    := ( { 1 + 1 })
    result := ptr.[idx]
    if result == 30 {0} else {1}
}

test_expression_position() {
    error := 0
    error += ep_test_block_as_field_init()
    error += ep_test_if_as_argument()
    error += ep_test_nested_if_both_branches()
    error += ep_test_block_in_arithmetic()
    error += ep_test_let_in_as_loop_bound()
    error += ep_test_deeply_nested_expression()
    error += ep_test_when_then_case()
    error += ep_test_block_as_index()
    error
}

// ============================================================
// From: test_stress_scope_shadow
// ============================================================

ss_test_deep_shadowing() {
    x := 1
    x := x + 1
    x := x + 1
    x := x + 1
    x := x + 1
    x := x + 1
    if x == 6 {0} else {1}
}

ss_test_block_shadowing() {
    x      := 10
    result := ( {
               x     := 20
               inner := ( {
               x     := 30
               x
               })
               x + inner
               })
    error := 0
    error += if result == 50 {0} else {1}
    error += if x == 10 {0} else {1}
    error
}

SSShape: | SSCircle { radius: Int }
         | SSSquare { length: Int }

ss_test_when_arm_shadowing() {
    s1 := SSCircle(5)
    s2 := SSSquare(10)

    r1 := when s1 {
        x: SSCircle { x.radius }
        x: SSSquare { x.length }
    }

    r2 := when s2 {
        x: SSCircle { x.radius }
        x: SSSquare { x.length }
    }

    error := 0
    error += if r1 == 5 {0} else {1}
    error += if r2 == 10 {0} else {1}
    error
}

ss_test_if_else_shadowing() {
    x := 100

    result := if 1 == 1 {
        x := 200
        x
    } else {
        x := 300
        x
    }

    error := 0
    error += if result == 200 {0} else {1}
    error += if x == 100 {0} else {1}
    error
}

ss_test_loop_rebind() {
    sum := 0
    i   := 0
    while i < 5, i = i + 1 {
        i := i * 10
        sum = sum + i
    }
    if sum == 100 {0} else {1}
}

ss_apply(f: () -> Int) -> Int { f() }

test_scope_shadow() {
    error := 0
    error += ss_test_deep_shadowing()
    error += ss_test_block_shadowing()
    error += ss_test_when_arm_shadowing()
    error += ss_test_if_else_shadowing()
    error += ss_test_loop_rebind()

    // Shadowing inside lambda captures
    ( {
     x := 10
     f := () { x }
     x := 20
     g := () { x }
     error += if f() == 10 {0} else {1}
     error += if g() == 20 {0} else {1}
     void
     })

    // Multiple shadows in nested function calls
    ( {
     x := 1
     f := () {
     x := 2
     x
     }
     result := ss_apply(f)
     error += if result == 2 {0} else {1}
     error += if x == 1 {0} else {1}
     void
     })

    error
}

// ============================================================
// From: test_stress_trait_generic_operators
// ============================================================

TGOAddEq[T]: Add[T], Eq[T] { }
TGOAddOrd[T]: Add[T], Ord[T] { }

tgo_double[T: Add](x: T) -> T { x + x }
tgo_are_equal[T: Eq](a: T, b: T) -> Bool { a == b }
tgo_is_less[T: Ord](a: T, b: T) -> Bool { a < b }
tgo_triple_add[T: Add](a: T, b: T, c: T) -> T { a + b + c }

tgo_add_if_different[T: TGOAddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

tgo_sum_if_ordered[T: TGOAddOrd](a: T, b: T) -> T {
    if a < b { a + b }
    else if a == b { a }
    else { b + a }
}

tgo_double_then_compare[T: TGOAddEq](a: T, b: T) -> Bool {
    da := tgo_double(a)
    db := tgo_double(b)
    tgo_are_equal(da, db)
}

tgo_wrap_sum[T: Add](a: T, b: T) -> TGOWrap.Wrapped[T] {
    TGOWrap.Wrapped.Has(v = a + b)
}

test_trait_generic_operators() {
    error := 0

    // Test with VecA.Vec2
    va1 := VecA.Vec2(x = 1, y = 2)
    va2 := VecA.Vec2(x = 3, y = 4)
    va3 := VecA.Vec2(x = 5, y = 6)

    d_va := tgo_double(va1)
    error += if d_va.x == 2 && d_va.y == 4 { 0 } else { 1 }

    error += if tgo_are_equal(va1, va1) { 0 } else { 1 }
    error += if tgo_are_equal(va1, va2) { 1 } else { 0 }

    error += if tgo_is_less(va1, va2) { 0 } else { 1 }
    error += if tgo_is_less(va2, va1) { 1 } else { 0 }

    t_va := tgo_triple_add(va1, va2, va3)
    error += if t_va.x == 9 && t_va.y == 12 { 0 } else { 1 }

    r_diff := tgo_add_if_different(va1, va2)
    error += if r_diff.x == 4 && r_diff.y == 6 { 0 } else { 1 }
    r_same := tgo_add_if_different(va1, va1)
    error += if r_same.x == 1 && r_same.y == 2 { 0 } else { 1 }

    r_ord := tgo_sum_if_ordered(va1, va2)
    error += if r_ord.x == 4 && r_ord.y == 6 { 0 } else { 1 }

    error += if tgo_double_then_compare(va1, va1) { 0 } else { 1 }
    error += if tgo_double_then_compare(va1, va2) { 1 } else { 0 }

    ws_va        := tgo_wrap_sum(va1, va2)
    sv: TGOWrap.Has := ws_va else { return 99 }
    error += if sv.v.x == 4 && sv.v.y == 6 { 0 } else { 1 }

    // Test with NumB.Num
    nb1 := NumB.Num(val = 10)
    nb2 := NumB.Num(val = 20)
    nb3 := NumB.Num(val = 30)

    d_nb := tgo_double(nb1)
    error += if d_nb.val == 20 { 0 } else { 1 }

    error += if tgo_are_equal(nb1, nb1) { 0 } else { 1 }
    error += if tgo_are_equal(nb1, nb2) { 1 } else { 0 }

    error += if tgo_is_less(nb1, nb2) { 0 } else { 1 }

    t_nb := tgo_triple_add(nb1, nb2, nb3)
    error += if t_nb.val == 60 { 0 } else { 1 }

    r_nb := tgo_add_if_different(nb1, nb2)
    error += if r_nb.val == 30 { 0 } else { 1 }

    r_nb_ord := tgo_sum_if_ordered(nb1, nb2)
    error += if r_nb_ord.val == 30 { 0 } else { 1 }

    error += if tgo_double_then_compare(nb1, nb1) { 0 } else { 1 }

    ws_nb          := tgo_wrap_sum(nb1, nb2)
    snb: TGOWrap.Has := ws_nb else { return 99 }
    error += if snb.v.val == 30 { 0 } else { 1 }

    // Test with TriC.Tri
    tc1 := TriC.Tri(a = 1, b = 2, c = 3)
    tc2 := TriC.Tri(a = 4, b = 5, c = 6)

    d_tc := tgo_double(tc1)
    error += if d_tc.a == 2 && d_tc.b == 4 && d_tc.c == 6 { 0 } else { 1 }

    tc3  := TriC.Tri(a = 7, b = 8, c = 9)
    t_tc := tgo_triple_add(tc1, tc2, tc3)
    error += if t_tc.a == 12 && t_tc.b == 15 && t_tc.c == 18 { 0 } else { 1 }

    r_tc := tgo_add_if_different(tc1, tc2)
    error += if r_tc.a == 5 && r_tc.b == 7 && r_tc.c == 9 { 0 } else { 1 }

    error
}

// ============================================================
// From: test_stress_trait_generic_operators_adversarial
// ============================================================

TGOAAddEq[T]:  Add[T], Eq[T]  { }
TGOAAddSub[T]: Add[T], Sub[T] { }
TGOAAddOrd[T]: Add[T], Ord[T] { }

tgoa_process_two[T: Add, U: Eq](a: T, b: T, x: U, y: U) -> Bool {
    sum := a + b
    x == y
}

tgoa_full_ord_test[T: Ord](a: T, b: T) -> Int {
    result := 0
    result += if a < b { 1 } else { 0 }
    result += if a <= b { 2 } else { 0 }
    result += if a > b { 4 } else { 0 }
    result += if a >= b { 8 } else { 0 }
    result += if a == b { 16 } else { 0 }
    result += if a != b { 32 } else { 0 }
    result
}

tgoa_add_if_different[T: TGOAAddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

tgoa_add_then_sub[T: TGOAAddSub](a: T, b: T, c: T) -> T {
    a + b - c
}

tgoa_inner_sum[T: Add] (a: T, b: T) -> T    { a + b  }
tgoa_outer_check[T: Eq](a: T, b: T) -> Bool { a == b }

tgoa_nested_sum_then_check[T: TGOAAddEq](a: T, b: T, expected: T) -> Bool {
    result := tgoa_inner_sum(a, b)
    tgoa_outer_check(result, expected)
}

tgoa_add_one[T: Add](a: T, b: T) -> T { a + b }

tgoa_chain_four_adds[T: Add](a: T, b: T, c: T, d: T) -> T {
    r1 := tgoa_add_one(a, b)
    r2 := tgoa_add_one(r1, c)
    tgoa_add_one(r2, d)
}

tgoa_min_val[T: Ord](a: T, b: T) -> T {
    if a <= b { a } else { b }
}

tgoa_max_val[T: Ord](a: T, b: T) -> T {
    if a >= b { a } else { b }
}

tgoa_clamp_val[T: Ord](val: T, lo: T, hi: T) -> T {
    tgoa_max_val(lo, tgoa_min_val(val, hi))
}

tgoa_sum_two_smallest[T: TGOAAddOrd](a: T, b: T, c: T) -> T {
    if a >= b && a >= c { b + c }
    else if b >= a && b >= c { a + c }
    else { a + b }
}

tgoa_compare_sums[T: Add, U: Add](a1: T, a2: T, b1: U, b2: U, eq_check: Bool) -> Bool {
    sum_t := a1 + a2
    sum_u := b1 + b2
    eq_check
}

test_trait_generic_operators_adversarial() {
    error := 0

    // Test 1: Two different bounded type params
    p1 := TGOAPair(x = 1, y = 2)
    p2 := TGOAPair(x = 3, y = 4)
    w1 := Weight(grams = 100)
    w2 := Weight(grams = 100)
    w3 := Weight(grams = 200)

    error += if tgoa_process_two(p1, p2, w1, w2) { 0 } else { 1 }
    error += if tgoa_process_two(p1, p2, w1, w3) { 1 } else { 0 }

    error += if tgoa_process_two(w1, w2, p1, p1) { 0 } else { 1 }
    error += if tgoa_process_two(w1, w2, p1, p2) { 1 } else { 0 }

    // Test 2: Ord inheritance
    bits_lt := tgoa_full_ord_test(Weight(grams = 10), Weight(grams = 20))
    error += if bits_lt == 35 { 0 } else { 1 }

    bits_eq := tgoa_full_ord_test(Weight(grams = 10), Weight(grams = 10))
    error += if bits_eq == 26 { 0 } else { 1 }

    bits_gt := tgoa_full_ord_test(Weight(grams = 20), Weight(grams = 10))
    error += if bits_gt == 44 { 0 } else { 1 }

    bits_lt2 := tgoa_full_ord_test(TGOAPair(x = 1, y = 2), TGOAPair(x = 3, y = 4))
    error += if bits_lt2 == 35 { 0 } else { 1 }

    bits_eq2 := tgoa_full_ord_test(TGOAPair(x = 5, y = 5), TGOAPair(x = 5, y = 5))
    error += if bits_eq2 == 26 { 0 } else { 1 }

    // Test 3: Combined traits
    r1 := tgoa_add_if_different(w1, w3)
    error += if r1.grams == 300 { 0 } else { 1 }
    r2 := tgoa_add_if_different(w1, w2)
    error += if r2.grams == 100 { 0 } else { 1 }

    r3 := tgoa_add_if_different(p1, p2)
    error += if r3.x == 4 && r3.y == 6 { 0 } else { 1 }
    r4 := tgoa_add_if_different(p1, p1)
    error += if r4.x == 1 && r4.y == 2 { 0 } else { 1 }

    s1 := Score(points = 10)
    s2 := Score(points = 20)
    s3 := Score(points = 5)
    r5 := tgoa_add_then_sub(s1, s2, s3)
    error += if r5.points == 25 { 0 } else { 1 }

    // Test 4: Nested generic calls
    expected_pair := TGOAPair(x = 4, y = 6)
    error += if tgoa_nested_sum_then_check(p1, p2, expected_pair) { 0 } else { 1 }

    expected_w := Weight(grams = 300)
    error += if tgoa_nested_sum_then_check(w1, w3, expected_w) { 0 } else { 1 }

    wrong_w := Weight(grams = 999)
    error += if tgoa_nested_sum_then_check(w1, w3, wrong_w) { 1 } else { 0 }

    // Test 5: Chained adds
    r_chain := tgoa_chain_four_adds(
                               Weight(grams = 10),
                               Weight(grams = 20),
                               Weight(grams = 30),
                               Weight(grams = 40)
                               )
    error += if r_chain.grams == 100 { 0 } else { 1 }

    r_chain_p := tgoa_chain_four_adds(
                                 TGOAPair(x = 1, y = 0),
                                 TGOAPair(x = 0, y = 1),
                                 TGOAPair(x = 2, y = 0),
                                 TGOAPair(x = 0, y = 2)
                                 )
    error += if r_chain_p.x == 3 && r_chain_p.y == 3 { 0 } else { 1 }

    // Test 6: Clamp via Ord
    clamped := tgoa_clamp_val(Weight(grams = 5), Weight(grams = 10), Weight(grams = 100))
    error += if clamped.grams == 10 { 0 } else { 1 }

    clamped2 := tgoa_clamp_val(Weight(grams = 500), Weight(grams = 10), Weight(grams = 100))
    error += if clamped2.grams == 100 { 0 } else { 1 }

    clamped3 := tgoa_clamp_val(Weight(grams = 50), Weight(grams = 10), Weight(grams = 100))
    error += if clamped3.grams == 50 { 0 } else { 1 }

    clamped_p := tgoa_clamp_val(TGOAPair(x = 0, y = 0), TGOAPair(x = 1, y = 1), TGOAPair(x = 9, y = 9))
    error += if clamped_p.x == 1 && clamped_p.y == 1 { 0 } else { 1 }

    // Test 7: Sum two smallest
    r_s2s := tgoa_sum_two_smallest(Score(points = 10), Score(points = 20), Score(points = 30))
    error += if r_s2s.points == 30 { 0 } else { 1 }

    r_s2s2 := tgoa_sum_two_smallest(Score(points = 20), Score(points = 5), Score(points = 10))
    error += if r_s2s2.points == 15 { 0 } else { 1 }

    r_s2sw := tgoa_sum_two_smallest(Weight(grams = 10), Weight(grams = 50), Weight(grams = 30))
    error += if r_s2sw.grams == 40 { 0 } else { 1 }

    // Test 8: Multiple bounded params
    error += if tgoa_compare_sums(p1, p2, w1, w3, true) { 0 } else { 1 }
    error += if tgoa_compare_sums(p1, p2, w1, w3, false) { 1 } else { 0 }

    error
}

// ============================================================
// From: test_stress_type_features
// ============================================================

TFShape: | TFCircle { radius: Int }
         | TFSquare { length: Int }
         | TFNone

tf_test_type_predicate_variant() {
    s      := TFCircle(5)
    result := when s {
        c: TFCircle {
            v := c.radius
            if v :: Int { v } else { -1 }
        }
        else { -1 }
    }
    if result == 5 {0} else {1}
}

tf_test_pointer_to_union() {
    s    := TFCircle(8)
    p    := s.&
    elem := p.*

    result := when elem {
        c:  TFCircle { c.radius }
        sq: TFSquare { sq.length }
        n:  TFNone { 0 }
    }
    if result == 8 {0} else {1}
}

tf_test_carray_of_unions() {
    arr: CArray[TFShape, 4] := void
    arr.[0] = TFCircle(1)
    arr.[1] = TFSquare(2)
    arr.[2] = TFCircle(3)
    arr.[3] = TFNone()

    sum               := 0
    ptr: Ptr[TFShape] := arr
    i                 := 0
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c:  TFCircle { c.radius }
            sq: TFSquare { sq.length }
            n:  TFNone { 0 }
        }
    }
    if sum == 6 {0} else {1}
}

TFContainer: | TFHolds { ptr: Ptr[Int], len: Int }
             | TFNothing

tf_test_variant_with_ptr_to_carray() {
    arr: CArray[Int, 4] := void
    p:   Ptr[Int]       := arr
    p.[0] = 10
    p.[1] = 20
    p.[2] = 30

    c := TFHolds(p, 3)

    result := when c {
        h: TFHolds {
            sum := 0
            j   := 0
            while j < h.len, j = j + 1 {
                sum = sum + h.ptr.[j]
            }
            sum
        }
        n: TFNothing { 0 }
    }
    if result == 60 {0} else {1}
}

TFIntList: | TFCons { head: Int, tail: Ptr[TFIntList] }
           | TFNil

tf_sum_list(list: TFIntList) -> Int {
    when list {
        c: TFCons {
            next := c.tail.*
            c.head + tf_sum_list(next)
        }
        n: TFNil { 0 }
    }
}

tf_test_recursive_union_deep() {
    nil: TFIntList := TFNil()
    n1             := TFCons(1, nil.&)
    n2             := TFCons(2, n1.&)
    n3             := TFCons(3, n2.&)
    n4             := TFCons(4, n3.&)
    n5             := TFCons(5, n4.&)

    result := tf_sum_list(n5)
    if result == 15 {0} else {1}
}

tf_test_mutable_when_via_ptr() {
    s := TFCircle(3)

    when s.& {
        c: TFCircle { c->radius = 99 }
        else { }
    }

    p      := s.&
    elem   := p.*
    result := when elem {
        c: TFCircle { c.radius }
        else { 0 }
    }
    if result == 99 {0} else {1}
}

tf_test_sizeof_union() {
    sz := sizeof[TFShape]()
    if sz > 0zu {0} else {1}
}

test_type_features() {
    error := 0
    error += tf_test_type_predicate_variant()
    error += tf_test_pointer_to_union()
    error += tf_test_carray_of_unions()
    error += tf_test_variant_with_ptr_to_carray()
    error += tf_test_recursive_union_deep()
    error += tf_test_mutable_when_via_ptr()
    error += tf_test_sizeof_union()
    error
}

// ============================================================
// From: test_stress_when_combinations
// ============================================================

WCShape: | WCCircle { radius: Int }
         | WCSquare { length: Int }
         | WCNone

WCColor: | WCRed  { intensity: Int }
         | WCBlue { intensity: Int }

wc_pick_value(x: Int) -> Int { x }

wc_test_when_as_argument() {
    s := WCCircle(5)

    result := wc_pick_value(when s {
                         c:  WCCircle { c.radius * 2 }
                         sq: WCSquare { sq.length }
                         n:  WCNone { 0 }
                         })
    if result == 10 {0} else {1}
}

wc_test_when_inside_when() {
    s := WCCircle(3)
    c := WCRed(10)

    result := when s {
        ci: WCCircle {
            when c {
                r: WCRed { ci.radius + r.intensity }
                b: WCBlue { ci.radius + b.intensity }
            }
        }
        sq: WCSquare { 0 }
        n:  WCNone { 0 }
    }
    if result == 13 {0} else {1}
}

wc_test_when_in_while() {
    shapes: CArray[WCShape, 3] := void
    shapes.[0] = WCCircle(1)
    shapes.[1] = WCSquare(2)
    shapes.[2] = WCCircle(3)

    sum               := 0
    i                 := 0
    ptr: Ptr[WCShape] := shapes
    while i < 3, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c:  WCCircle { c.radius }
            sq: WCSquare { sq.length }
            n:  WCNone { 0 }
        }
    }
    if sum == 6 {0} else {1}
}

wc_test_when_arithmetic() {
    a := WCCircle(10)
    b := WCSquare(5)

    sum := when a {
        c: WCCircle { c.radius }
        else { 0 }
    } + when b {
        sq: WCSquare { sq.length }
        else { 0 }
    }
    if sum == 15 {0} else {1}
}

wc_test_mutable_nested_in_immutable() {
    outer := WCCircle(100)
    inner := WCSquare(7)

    result := when outer {
        c: WCCircle {
            when inner.& {
                sq: WCSquare { sq->length = 42
                    sq->length }
                else { 0 }
            }
        }
        else { 0 }
    }
    check := when inner {
        sq: WCSquare { sq.length }
        else { 0 }
    }
    error := 0
    error += if result == 42 {0} else {1}
    error += if check == 42 {0} else {1}
    error
}

wc_test_when_chained_else() {
    a := WCNone()
    b := WCSquare(8)

    result := when a {
        c: WCCircle { c.radius }
        else {
            when b {
                sq: WCSquare { sq.length }
                else { -1 }
            }
        }
    }
    if result == 8 {0} else {1}
}

test_when_combinations() {
    error := 0
    error += wc_test_when_as_argument()
    error += wc_test_when_inside_when()
    error += wc_test_when_in_while()
    error += wc_test_when_arithmetic()
    error += wc_test_mutable_nested_in_immutable()
    error += wc_test_when_chained_else()
    error
}

// ============================================================
// Main
// ============================================================

main() {
    error: CInt := 0
    error += test_control_flow()
    error += test_deep_nesting()
    error += test_expression_position()
    error += test_scope_shadow()
    error += test_trait_generic_operators()
    error += test_trait_generic_operators_adversarial()
    error += test_type_features()
    error += test_when_combinations()
    error
}
