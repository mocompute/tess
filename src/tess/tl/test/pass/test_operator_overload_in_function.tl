// Operator overloading on user-defined types should work inside
// non-main functions. Currently triggers a spurious CInt vs CLongLong
// type conflict during specialization.

#module Num

Number:                             { val: Int                    }
eq(a: Number, b: Number) -> Bool    { a.val == b.val              }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }

#module main

test_eq() {
    a     := Num.Number(val = 1)
    b     := Num.Number(val = 1)
    c     := Num.Number(val = 2)
    error := 0
    error += if a == b { 0 } else { 1 }
    error += if a == c { 1 } else { 0 }
    error
}

test_add() {
    a := Num.Number(val = 3)
    b := Num.Number(val = 4)
    c := a + b
    if c.val == 7 { 0 } else { 1 }
}

main() {
    error := 0
    error += test_eq()
    error += test_add()
    error
}
