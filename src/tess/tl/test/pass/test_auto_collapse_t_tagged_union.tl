// Test auto-collapse with generic tagged unions and module functions.

#module Opt

T[a]: | Has { v: a }
      | Empty

my_unwrap[a](opt: T[a], default: a) -> a {
    when opt {
        s: Has { s.v }
        n: Empty { default }
    }
}

#module main

main() {
    error := 0

    // Use auto-collapsed alias "Opt" for "Opt.T"
    a := Opt.Has(42)
    b: Opt[Int] := Opt.Empty

    // Call generic function on auto-collapsed type
    error += if Opt.my_unwrap(a, 0) == 42 { 0 } else { 1 }
    error += if Opt.my_unwrap(b, 99) == 99 { 0 } else { 1 }

    // Float variant
    af := Opt.Has(3.14)
    error += if Opt.my_unwrap(af, 0.0) == 3.14 { 0 } else { 1 }

    error
}
