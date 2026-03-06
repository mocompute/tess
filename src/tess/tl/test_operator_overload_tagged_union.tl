// Test operator overloading on tagged union types.

#module Opt

OptInt: | Some { v: Int } | None

eq(a: OptInt, b: OptInt) -> Bool {
    a :: Some && b :: Some && a.Some.v == b.Some.v ||
    a :: None && b :: None
}

add(a: OptInt, b: OptInt) -> OptInt {
    if a :: Some && b :: Some { OptInt.Some(v = a.Some.v + b.Some.v) }
    else { OptInt.None }
}

#module main

main() {
    error := 0

    a := Opt.OptInt.Some(v = 10)
    b := Opt.OptInt.Some(v = 20)
    n := Opt.OptInt.None

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
    error += if c == Opt.OptInt.Some(v = 30) {0} else {1}

    // + with None propagates
    d := a + n
    error += if d == n {0} else {1}

    error
}
