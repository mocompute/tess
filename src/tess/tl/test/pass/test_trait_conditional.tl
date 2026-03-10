#module Wrapper

// A simple generic wrapper type
Pair[A]: { fst: A, snd: A }

// Conditional conformance: Pair[T] satisfies Eq when T satisfies Eq
eq[T: Eq](a: Pair[T], b: Pair[T]) -> Bool {
    a.fst == b.fst && a.snd == b.snd
}

// Conditional conformance: Pair[T] satisfies Add when T satisfies Add
add[T: Add](a: Pair[T], b: Pair[T]) -> Pair[T] {
    Pair(fst = a.fst + b.fst, snd = a.snd + b.snd)
}


#module Container

// A second generic wrapper — enables transitive conditional conformance
Box[A]: { inner: A }

eq[T: Eq](a: Box[T], b: Box[T]) -> Bool {
    a.inner == b.inner
}

add[T: Add](a: Box[T], b: Box[T]) -> Box[T] {
    Box(inner = a.inner + b.inner)
}


#module Num

Number: { val: Int }

eq (a: Number, b: Number) -> Bool   { a.val == b.val              }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }


#module main

// Generic functions requiring trait bounds — used to exercise conditional conformance
are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

sum_two[T: Add](a: T, b: T) -> T {
    a + b
}

main() {
    error := 0

    // =========================================================================
    // Basic conditional conformance: Pair[Number]: Eq because Number: Eq
    // =========================================================================

    a := Wrapper.Pair(fst = Num.Number(val = 1), snd = Num.Number(val = 2))
    b := Wrapper.Pair(fst = Num.Number(val = 1), snd = Num.Number(val = 2))
    c := Wrapper.Pair(fst = Num.Number(val = 3), snd = Num.Number(val = 4))

    // Direct operator use
    error += if a == b { 0 } else { 1 }
    error += if a == c { 1 } else { 0 }
    error += if a != c { 0 } else { 1 }
    error += if a != b { 1 } else { 0 }

    // Through bounded generic — triggers conditional conformance check
    error += if are_equal(a, b) { 0 } else { 1 }
    error += if are_equal(a, c) { 1 } else { 0 }

    // Conditional conformance: Pair[Number]: Add because Number: Add
    d := a + c
    error += if d.fst.val == 4 && d.snd.val == 6 { 0 } else { 1 }

    e := sum_two(a, c)
    error += if e.fst.val == 4 && e.snd.val == 6 { 0 } else { 1 }

    // =========================================================================
    // Transitive conditional conformance (3 levels deep):
    // Box[Pair[Number]]: Eq requires Pair[Number]: Eq requires Number: Eq
    // =========================================================================

    ba := Container.Box(inner = Wrapper.Pair(fst = Num.Number(val = 1), snd = Num.Number(val = 2)))
    bb := Container.Box(inner = Wrapper.Pair(fst = Num.Number(val = 1), snd = Num.Number(val = 2)))
    bc := Container.Box(inner = Wrapper.Pair(fst = Num.Number(val = 3), snd = Num.Number(val = 4)))

    // Direct operators at 3 levels
    error += if ba == bb { 0 } else { 1 }
    error += if ba == bc { 1 } else { 0 }

    // Through bounded generic at 3 levels
    error += if are_equal(ba, bb) { 0 } else { 1 }
    error += if are_equal(ba, bc) { 1 } else { 0 }

    // Transitive Add: Box[Pair[Number]] + Box[Pair[Number]]
    bd := sum_two(ba, bc)
    error += if bd.inner.fst.val == 4 && bd.inner.snd.val == 6 { 0 } else { 1 }

    // =========================================================================
    // Built-in inner type: Pair[Int]: Eq — Int implicitly satisfies Eq
    // =========================================================================

    ia := Wrapper.Pair(fst = 10, snd = 20)
    ib := Wrapper.Pair(fst = 10, snd = 20)
    ic := Wrapper.Pair(fst = 30, snd = 40)

    error += if ia == ib { 0 } else { 1 }
    error += if ia == ic { 1 } else { 0 }
    error += if are_equal(ia, ib) { 0 } else { 1 }
    error += if are_equal(ia, ic) { 1 } else { 0 }

    // Pair[Int]: Add — Int implicitly satisfies Add
    id := ia + ic
    error += if id.fst == 40 && id.snd == 60 { 0 } else { 1 }
    ie := sum_two(ia, ic)
    error += if ie.fst == 40 && ie.snd == 60 { 0 } else { 1 }

    // Box[Int]: Eq — single level with built-in inner
    x := Container.Box(inner = 5)
    y := Container.Box(inner = 5)
    z := Container.Box(inner = 9)
    error += if are_equal(x, y) { 0 } else { 1 }
    error += if are_equal(x, z) { 1 } else { 0 }

    // =========================================================================
    // Transitive with built-in at the leaf: Box[Pair[Int]]
    // Box[Pair[Int]]: Eq requires Pair[Int]: Eq requires Int: Eq (built-in)
    // =========================================================================

    ta := Container.Box(inner = Wrapper.Pair(fst = 1, snd = 2))
    tb := Container.Box(inner = Wrapper.Pair(fst = 1, snd = 2))
    tc := Container.Box(inner = Wrapper.Pair(fst = 9, snd = 9))

    error += if are_equal(ta, tb) { 0 } else { 1 }
    error += if are_equal(ta, tc) { 1 } else { 0 }

    // =========================================================================
    // Compound assignment on conditionally conforming types
    // =========================================================================

    ca := Wrapper.Pair(fst = Num.Number(val = 1), snd = Num.Number(val = 2))
    cb := Wrapper.Pair(fst = Num.Number(val = 3), snd = Num.Number(val = 4))
    ca += cb
    error += if ca.fst.val == 4 && ca.snd.val == 6 { 0 } else { 1 }

    // Compound assignment with built-in inner type
    ci := Wrapper.Pair(fst = 10, snd = 20)
    cj := Wrapper.Pair(fst = 5, snd = 15)
    ci += cj
    error += if ci.fst == 15 && ci.snd == 35 { 0 } else { 1 }

    // =========================================================================
    // Multiple instantiations of the same bounded generic with different types
    // =========================================================================

    // are_equal with Pair[Number]
    error += if are_equal(a, b) { 0 } else { 1 }
    // are_equal with Box[Number]
    m1 := Container.Box(inner = Num.Number(val = 42))
    m2 := Container.Box(inner = Num.Number(val = 42))
    m3 := Container.Box(inner = Num.Number(val = 99))
    error += if are_equal(m1, m2) { 0 } else { 1 }
    error += if are_equal(m1, m3) { 1 } else { 0 }
    // are_equal with Pair[Int]
    error += if are_equal(ia, ib) { 0 } else { 1 }
    // are_equal with plain Number (non-conditional)
    error += if are_equal(Num.Number(val = 5), Num.Number(val = 5)) { 0 } else { 1 }
    error += if are_equal(Num.Number(val = 5), Num.Number(val = 6)) { 1 } else { 0 }

    // sum_two with Pair[Number] and Pair[Int] (different conditional instantiations)
    sa := sum_two(a, c)
    error += if sa.fst.val == 4 { 0 } else { 1 }
    si := sum_two(ia, ic)
    error += if si.fst == 40 { 0 } else { 1 }

    error
}
