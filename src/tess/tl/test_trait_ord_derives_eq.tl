// Ord conformance derives eq from cmp: a type with only cmp should satisfy
// both Ord and Eq, since equality is derivable from ordering (cmp(a,b) == 0).

#module Foo

Thing : { val: Int }

cmp(a: Thing, b: Thing) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


#module main

is_sorted[T: Ord](a: T, b: T) -> Bool {
    a <= b
}

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    error := 0

    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 2)
    c := Foo.Thing(val = 1)

    // Ord bound works with only cmp
    error += if is_sorted(a, b) { 0 } else { 1 }
    error += if is_sorted(b, a) { 1 } else { 0 }

    // Eq bound also works — eq derived from cmp
    error += if are_equal(a, c) { 0 } else { 1 }
    error += if are_equal(a, b) { 1 } else { 0 }

    // != through Eq bound also works
    error += if a != b { 0 } else { 1 }
    error += if a != c { 1 } else { 0 }

    error
}
