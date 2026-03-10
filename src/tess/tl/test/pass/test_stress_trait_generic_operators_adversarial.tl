// Adversarial stress test: trait-bounded generics with operator overloading.
// Exercises multi-param bounds, Ord inheritance, deeply nested specialization,
// and cross-type trait bound interactions.

// --- Module P: a pair type with Add, Eq, Ord ---
#module Pair

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

// --- Module W: a weight type with Add, Eq, Ord ---
#module Weight

T: { grams: Int }

add(a: T, b: T) -> T    { T(grams = a.grams + b.grams) }
eq (a: T, b: T) -> Bool { a.grams == b.grams           }

cmp(a: T, b: T) -> CInt {
    if a.grams < b.grams { -1 }
    else if a.grams > b.grams { 1 }
    else { 0 }
}

// --- Module S: a score type with Add, Eq, Ord, Sub ---
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


#module main

// --- 1. Two DIFFERENT trait-bounded type parameters ---
// T: Add, U: Eq -- uses + on T and == on U independently
process_two[T: Add, U: Eq](a: T, b: T, x: U, y: U) -> Bool {
    sum := a + b
    x == y
}

// --- 2. Ord bound (inherits Eq) -- verify ALL derived operators ---
// Ord provides <, <=, >, >= and Eq provides ==, !=
// Encode which operators returned true as a bitmask.
full_ord_test[T: Ord](a: T, b: T) -> Int {
    result := 0
    result += if a < b { 1 } else { 0 }
    result += if a <= b { 2 } else { 0 }
    result += if a > b { 4 } else { 0 }
    result += if a >= b { 8 } else { 0 }
    result += if a == b { 16 } else { 0 }
    result += if a != b { 32 } else { 0 }
    result
}

// --- 3. Combined traits ---
AddEq[T]:  Add[T], Eq[T]  { }
AddSub[T]: Add[T], Sub[T] { }

// Uses both + and == in one function
add_if_different[T: AddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

// Uses both + and - in one function
add_then_sub[T: AddSub](a: T, b: T, c: T) -> T {
    a + b - c
}

// --- 4. Nested generic calls at different trait levels ---
inner_sum[T:   Add](a: T, b: T) -> T    { a + b  }
outer_check[T: Eq] (a: T, b: T) -> Bool { a == b }

nested_sum_then_check[T: AddEq](a: T, b: T, expected: T) -> Bool {
    result := inner_sum(a, b)
    outer_check(result, expected)
}

// --- 5. Deeply chained generic calls ---
add_one[T: Add](a: T, b: T) -> T { a + b }

chain_four_adds[T: Add](a: T, b: T, c: T, d: T) -> T {
    r1 := add_one(a, b)
    r2 := add_one(r1, c)
    add_one(r2, d)
}

// --- 6. Ord with min/max/clamp, specialized at multiple types ---
min_val[T: Ord](a: T, b: T) -> T {
    if a <= b { a } else { b }
}

max_val[T: Ord](a: T, b: T) -> T {
    if a >= b { a } else { b }
}

clamp_val[T: Ord](val: T, lo: T, hi: T) -> T {
    max_val(lo, min_val(val, hi))
}

// --- 7. Trait bounds with conditional logic across operators ---
// Given three values, return the sum of the two smallest (requires Ord + Add)
AddOrd[T]: Add[T], Ord[T] { }

sum_two_smallest[T: AddOrd](a: T, b: T, c: T) -> T {
    if a >= b && a >= c { b + c }
    else if b >= a && b >= c { a + c }
    else { a + b }
}

// --- 8. Multiple bounded params where both are used with operators ---
compare_sums[T: Add, U: Add](a1: T, a2: T, b1: U, b2: U, eq_check: Bool) -> Bool {
    sum_t := a1 + a2
    sum_u := b1 + b2
    eq_check
}


main() {
    error := 0

    // --- Test 1: Two different bounded type params ---
    p1 := Pair(x = 1, y = 2)
    p2 := Pair(x = 3, y = 4)
    w1 := Weight(grams = 100)
    w2 := Weight(grams = 100)
    w3 := Weight(grams = 200)

    // T=Pair (Add), U=Weight (Eq)
    error += if process_two(p1, p2, w1, w2) { 0 } else { 1 }
    error += if process_two(p1, p2, w1, w3) { 1 } else { 0 }

    // T=Weight (Add), U=Pair (Eq)
    error += if process_two(w1, w2, p1, p1) { 0 } else { 1 }
    error += if process_two(w1, w2, p1, p2) { 1 } else { 0 }

    // --- Test 2: Ord inheritance -- all comparison operators ---
    // a < b: 1 + 2 + 32 = 35
    bits_lt := full_ord_test(Weight(grams = 10), Weight(grams = 20))
    error += if bits_lt == 35 { 0 } else { 1 }

    // a == b: 2 + 8 + 16 = 26
    bits_eq := full_ord_test(Weight(grams = 10), Weight(grams = 10))
    error += if bits_eq == 26 { 0 } else { 1 }

    // a > b: 4 + 8 + 32 = 44
    bits_gt := full_ord_test(Weight(grams = 20), Weight(grams = 10))
    error += if bits_gt == 44 { 0 } else { 1 }

    // Same tests with Pair type
    bits_lt2 := full_ord_test(Pair(x = 1, y = 2), Pair(x = 3, y = 4))
    error += if bits_lt2 == 35 { 0 } else { 1 }

    bits_eq2 := full_ord_test(Pair(x = 5, y = 5), Pair(x = 5, y = 5))
    error += if bits_eq2 == 26 { 0 } else { 1 }

    // --- Test 3: Combined traits ---
    // add_if_different with Weight
    r1 := add_if_different(w1, w3)
    error += if r1.grams == 300 { 0 } else { 1 }
    r2 := add_if_different(w1, w2)
    error += if r2.grams == 100 { 0 } else { 1 }

    // add_if_different with Pair
    r3 := add_if_different(p1, p2)
    error += if r3.x == 4 && r3.y == 6 { 0 } else { 1 }
    r4 := add_if_different(p1, p1)
    error += if r4.x == 1 && r4.y == 2 { 0 } else { 1 }

    // add_then_sub with Score
    s1 := Score(points = 10)
    s2 := Score(points = 20)
    s3 := Score(points = 5)
    r5 := add_then_sub(s1, s2, s3)
    error += if r5.points == 25 { 0 } else { 1 }

    // --- Test 4: Nested generic calls with different trait bounds ---
    expected_pair := Pair(x = 4, y = 6)
    error += if nested_sum_then_check(p1, p2, expected_pair) { 0 } else { 1 }

    expected_w := Weight(grams = 300)
    error += if nested_sum_then_check(w1, w3, expected_w) { 0 } else { 1 }

    wrong_w := Weight(grams = 999)
    error += if nested_sum_then_check(w1, w3, wrong_w) { 1 } else { 0 }

    // --- Test 5: Chained adds through multiple generic calls ---
    r_chain := chain_four_adds(
                               Weight(grams = 10),
                               Weight(grams = 20),
                               Weight(grams = 30),
                               Weight(grams = 40)
                               )
    error += if r_chain.grams == 100 { 0 } else { 1 }

    r_chain_p := chain_four_adds(
                                 Pair(x = 1, y = 0),
                                 Pair(x = 0, y = 1),
                                 Pair(x = 2, y = 0),
                                 Pair(x = 0, y = 2)
                                 )
    error += if r_chain_p.x == 3 && r_chain_p.y == 3 { 0 } else { 1 }

    // --- Test 6: Clamp via Ord ---
    clamped := clamp_val(Weight(grams = 5), Weight(grams = 10), Weight(grams = 100))
    error += if clamped.grams == 10 { 0 } else { 1 }

    clamped2 := clamp_val(Weight(grams = 500), Weight(grams = 10), Weight(grams = 100))
    error += if clamped2.grams == 100 { 0 } else { 1 }

    clamped3 := clamp_val(Weight(grams = 50), Weight(grams = 10), Weight(grams = 100))
    error += if clamped3.grams == 50 { 0 } else { 1 }

    // Clamp with Pair
    clamped_p := clamp_val(Pair(x = 0, y = 0), Pair(x = 1, y = 1), Pair(x = 9, y = 9))
    error += if clamped_p.x == 1 && clamped_p.y == 1 { 0 } else { 1 }

    // --- Test 7: Sum two smallest ---
    // 10 + 20 = 30 (30 is largest)
    r_s2s := sum_two_smallest(Score(points = 10), Score(points = 20), Score(points = 30))
    error += if r_s2s.points == 30 { 0 } else { 1 }

    // 5 + 10 = 15 (20 is largest)
    r_s2s2 := sum_two_smallest(Score(points = 20), Score(points = 5), Score(points = 10))
    error += if r_s2s2.points == 15 { 0 } else { 1 }

    // With Weight: 10 + 30 = 40 (50 is largest)
    r_s2sw := sum_two_smallest(Weight(grams = 10), Weight(grams = 50), Weight(grams = 30))
    error += if r_s2sw.grams == 40 { 0 } else { 1 }

    // --- Test 8: Multiple bounded params both used with operators ---
    error += if compare_sums(p1, p2, w1, w3, true) { 0 } else { 1 }
    error += if compare_sums(p1, p2, w1, w3, false) { 1 } else { 0 }

    error
}
