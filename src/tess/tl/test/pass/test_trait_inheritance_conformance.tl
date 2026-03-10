// Trait inheritance in conformance checking: a bound on a user-defined child
// trait should require functions from all parent traits.

#module Num

Number: { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
sub(a: Number, b: Number) -> Number { Number(val = a.val - b.val) }
mul(a: Number, b: Number) -> Number { Number(val = a.val * b.val) }
div(a: Number, b: Number) -> Number { Number(val = a.val / b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }

#module main

// User-defined combined trait: requires Add, Sub, Mul, Div
Numeric[T]: Add[T], Sub[T], Mul[T], Div[T] { }

// Function using the combined trait
eval[T: Numeric](a: T, b: T) -> T {
    a + b - a * b
}

main() {
    error := 0

    a := Num.Number(val = 3)
    b := Num.Number(val = 2)

    // Number satisfies Numeric (has add, sub, mul, div)
    r := eval(a, b)
    // 3 + 2 - 3 * 2 = 5 - 6 = -1
    error += if r.val == 0 - 1 {0} else {1}

    error
}
