// Diamond inheritance: D inherits from B and C, both of which inherit from A.
// The compiler should deduplicate A's requirements.

#module Num

Number : { val: Int }

eq(a: Number, b: Number) -> Bool { a.val == b.val }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }

cmp(a: Number, b: Number) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}

#module main

// A is the base trait
// B and C both inherit from Eq
// D inherits from both B and C — diamond pattern
B[T] : Eq[T] {
    foo(a: T) -> T
}

C[T] : Eq[T] {
    bar(a: T) -> T
}

D[T] : B[T], C[T] { }

main() { 0 }
