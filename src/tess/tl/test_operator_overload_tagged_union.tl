// Test operator overloading on tagged union types.

#module Opt

OptInt: | Val { v: Int } | Empty

eq(a: OptInt, b: OptInt) -> Bool {
    when a {
        sa: Val {
            sb: Val := b else { return false }
            sa.v == sb.v
        }
        na: Empty {
            when b {
                nb: Empty { true }
                else { false }
            }
        }
    }
}

add(a: OptInt, b: OptInt) -> OptInt {
    sa: Val := a else { return OptInt.Empty }
    sb: Val := b else { return OptInt.Empty }
    OptInt.Val(v = sa.v + sb.v)
}

#module main

main() {
    error := 0

    a := Opt.OptInt.Val(v = 10)
    b := Opt.OptInt.Val(v = 20)
    n := Opt.OptInt.Empty

    // == on tagged union
    error += if a == a {0} else {1}
    error += if a == b {1} else {0}
    error += if n == n {0} else {1}
    error += if a == n {1} else {0}

    // != derived from eq
    error += if a != b {0} else {1}
    error += if a != a {1} else {0}

    // + on tagged union
    c := a + b
    error += if c == Opt.OptInt.Val(v = 30) {0} else {1}

    // + with None propagates
    d := a + n
    error += if d == n {0} else {1}

    error
}
