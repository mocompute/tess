#module Num

Number : { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
sub(a: Number, b: Number) -> Number { Number(val = a.val - b.val) }
eq(a: Number, b: Number) -> Bool { a.val == b.val }

cmp(a: Number, b: Number) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


#module main

// User-defined combined trait requiring both Add and Eq
AddEq[T] : Add[T], Eq[T] { }

// Function using the combined trait
add_if_different[T: AddEq](a: T, b: T) -> T {
    if a == b { a }
    else { a + b }
}

// Function requiring Ord (which inherits Eq, so both eq and cmp needed)
clamp[T: Ord](val: T, lo: T, hi: T) -> T {
    if val < lo { lo }
    else if val > hi { hi }
    else { val }
}

// Multiple bounded type params
pick_lesser[T: Ord, U: Eq](a: T, b: T, x: U, y: U) -> T {
    // Use T's Ord and U's Eq independently
    if x == y {
        if a < b { a } else { b }
    } else {
        if a > b { a } else { b }
    }
}

main() {
    error := 0

    a := Num.Number(val = 3)
    b := Num.Number(val = 7)
    same := Num.Number(val = 3)

    // add_if_different: different values -> adds them
    r1 := add_if_different(a, b)
    error += if r1.val == 10 {0} else {1}

    // add_if_different: same values -> returns first
    r2 := add_if_different(a, same)
    error += if r2.val == 3 {0} else {1}

    // clamp: value in range
    r3 := clamp(Num.Number(val = 5), a, b)
    error += if r3.val == 5 {0} else {1}

    // clamp: value below range
    r4 := clamp(Num.Number(val = 1), a, b)
    error += if r4.val == 3 {0} else {1}

    // clamp: value above range
    r5 := clamp(Num.Number(val = 10), a, b)
    error += if r5.val == 7 {0} else {1}

    // pick_lesser with matching second pair (picks lesser of first pair)
    r6 := pick_lesser(b, a, same, a)
    error += if r6.val == 3 {0} else {1}

    // pick_lesser with non-matching second pair (picks greater of first pair)
    r7 := pick_lesser(a, b, a, b)
    error += if r7.val == 7 {0} else {1}

    error
}
