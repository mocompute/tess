// Receiver block with multiple parameters.

#module Pair

Pair: { a: Int, b: Int }

// Two-parameter block: both parameters prepended.
(x: Ptr[Pair], y: Ptr[Pair]): {
    sum_a() -> Int {
        x->a + y->a
    }

    eq() -> Bool {
        x->a == y->a && x->b == y->b
    }
}

#module main

main() {
    error := 0
    p1    := Pair.Pair(a = 1, b = 2)
    p2    := Pair.Pair(a = 3, b = 4)
    p3    := Pair.Pair(a = 1, b = 2)

    error += if Pair.sum_a(p1.&, p2.&) == 4 {0} else {1}
    error += if Pair.eq(p1.&, p3.&) == true {0} else {1}
    error += if Pair.eq(p1.&, p2.&) == false {0} else {1}

    error
}
