// Test division and modulo operator overloading on user-defined types.

#module Num

Number : { val: Int }

div(a: Number, b: Number) -> Number { Number(val = a.val / b.val) }
mod(a: Number, b: Number) -> Number { Number(val = a.val % b.val) }
eq(a: Number, b: Number) -> Bool { a.val == b.val }

#module main

main() {
    error := 0

    a := Num.Number(val = 17)
    b := Num.Number(val = 5)

    // / dispatches to Num.div
    c := a / b
    error += if c.val == 3 {0} else {1}

    // % dispatches to Num.mod
    d := a % b
    error += if d.val == 2 {0} else {1}

    // Compound assignment /=
    e := Num.Number(val = 20)
    e /= b
    error += if e.val == 4 {0} else {1}

    // Compound assignment %=
    f := Num.Number(val = 17)
    f %= b
    error += if f.val == 2 {0} else {1}

    error
}
