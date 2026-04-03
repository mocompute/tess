// Combined trait misc tests:
//   test_trait_conditional
//   test_trait_declaration
//   test_trait_diamond_inheritance
//   test_trait_inheritance_conformance
//   test_trait_ord_derives_eq
//   test_trait_ptr_receiver_builtin_int

// --- Modules for test_trait_conditional ---

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


#module CondNum

Number: { val: Int }

eq (a: Number, b: Number) -> Bool   { a.val == b.val              }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }


// --- Module for test_trait_diamond_inheritance ---

#module DiamondNum

Number: { val: Int }

eq (a: Number, b: Number) -> Bool   { a.val == b.val              }
add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }

cmp(a: Number, b: Number) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


// --- Module for test_trait_inheritance_conformance ---

#module InheritNum

Number: { val: Int }

add(a: Number, b: Number) -> Number { Number(val = a.val + b.val) }
sub(a: Number, b: Number) -> Number { Number(val = a.val - b.val) }
mul(a: Number, b: Number) -> Number { Number(val = a.val * b.val) }
div(a: Number, b: Number) -> Number { Number(val = a.val / b.val) }
eq (a: Number, b: Number) -> Bool   { a.val == b.val              }


// --- Module for test_trait_ord_derives_eq ---

#module Foo

Thing: { val: Int }

cmp(a: Thing, b: Thing) -> CInt {
    if a.val < b.val { -1 }
    else if a.val > b.val { 1 }
    else { 0 }
}


// --- Module for test_trait_ptr_receiver_builtin_int ---

#module Mutate

Mutate[T]: {
    mutate(self: Ptr[T]) -> CInt
}

#module Int
mutate(self) -> CInt { self.* = self.* + 1; 1 }


#module main

// === test_trait_conditional ===

cond_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

cond_sum_two[T: Add](a: T, b: T) -> T {
    a + b
}

test_trait_conditional() {
    error := 0

    // =========================================================================
    // Basic conditional conformance: Pair[Number]: Eq because Number: Eq
    // =========================================================================

    a := Wrapper.Pair(fst = CondNum.Number(val = 1), snd = CondNum.Number(val = 2))
    b := Wrapper.Pair(fst = CondNum.Number(val = 1), snd = CondNum.Number(val = 2))
    c := Wrapper.Pair(fst = CondNum.Number(val = 3), snd = CondNum.Number(val = 4))

    // Direct operator use
    error += if a == b { 0 } else { 1 }
    error += if a == c { 1 } else { 0 }
    error += if a != c { 0 } else { 1 }
    error += if a != b { 1 } else { 0 }

    // Through bounded generic — triggers conditional conformance check
    error += if cond_are_equal(a, b) { 0 } else { 1 }
    error += if cond_are_equal(a, c) { 1 } else { 0 }

    // Conditional conformance: Pair[Number]: Add because Number: Add
    d := a + c
    error += if d.fst.val == 4 && d.snd.val == 6 { 0 } else { 1 }

    e := cond_sum_two(a, c)
    error += if e.fst.val == 4 && e.snd.val == 6 { 0 } else { 1 }

    // =========================================================================
    // Transitive conditional conformance (3 levels deep):
    // Box[Pair[Number]]: Eq requires Pair[Number]: Eq requires Number: Eq
    // =========================================================================

    ba := Container.Box(inner = Wrapper.Pair(fst = CondNum.Number(val = 1), snd = CondNum.Number(val = 2)))
    bb := Container.Box(inner = Wrapper.Pair(fst = CondNum.Number(val = 1), snd = CondNum.Number(val = 2)))
    bc := Container.Box(inner = Wrapper.Pair(fst = CondNum.Number(val = 3), snd = CondNum.Number(val = 4)))

    // Direct operators at 3 levels
    error += if ba == bb { 0 } else { 1 }
    error += if ba == bc { 1 } else { 0 }

    // Through bounded generic at 3 levels
    error += if cond_are_equal(ba, bb) { 0 } else { 1 }
    error += if cond_are_equal(ba, bc) { 1 } else { 0 }

    // Transitive Add: Box[Pair[Number]] + Box[Pair[Number]]
    bd := cond_sum_two(ba, bc)
    error += if bd.inner.fst.val == 4 && bd.inner.snd.val == 6 { 0 } else { 1 }

    // =========================================================================
    // Built-in inner type: Pair[Int]: Eq — Int implicitly satisfies Eq
    // =========================================================================

    ia := Wrapper.Pair(fst = 10, snd = 20)
    ib := Wrapper.Pair(fst = 10, snd = 20)
    ic := Wrapper.Pair(fst = 30, snd = 40)

    error += if ia == ib { 0 } else { 1 }
    error += if ia == ic { 1 } else { 0 }
    error += if cond_are_equal(ia, ib) { 0 } else { 1 }
    error += if cond_are_equal(ia, ic) { 1 } else { 0 }

    // Pair[Int]: Add — Int implicitly satisfies Add
    id := ia + ic
    error += if id.fst == 40 && id.snd == 60 { 0 } else { 1 }
    ie := cond_sum_two(ia, ic)
    error += if ie.fst == 40 && ie.snd == 60 { 0 } else { 1 }

    // Box[Int]: Eq — single level with built-in inner
    x := Container.Box(inner = 5)
    y := Container.Box(inner = 5)
    z := Container.Box(inner = 9)
    error += if cond_are_equal(x, y) { 0 } else { 1 }
    error += if cond_are_equal(x, z) { 1 } else { 0 }

    // =========================================================================
    // Transitive with built-in at the leaf: Box[Pair[Int]]
    // Box[Pair[Int]]: Eq requires Pair[Int]: Eq requires Int: Eq (built-in)
    // =========================================================================

    ta := Container.Box(inner = Wrapper.Pair(fst = 1, snd = 2))
    tb := Container.Box(inner = Wrapper.Pair(fst = 1, snd = 2))
    tc := Container.Box(inner = Wrapper.Pair(fst = 9, snd = 9))

    error += if cond_are_equal(ta, tb) { 0 } else { 1 }
    error += if cond_are_equal(ta, tc) { 1 } else { 0 }

    // =========================================================================
    // Compound assignment on conditionally conforming types
    // =========================================================================

    ca := Wrapper.Pair(fst = CondNum.Number(val = 1), snd = CondNum.Number(val = 2))
    cb := Wrapper.Pair(fst = CondNum.Number(val = 3), snd = CondNum.Number(val = 4))
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
    error += if cond_are_equal(a, b) { 0 } else { 1 }
    // are_equal with Box[Number]
    m1 := Container.Box(inner = CondNum.Number(val = 42))
    m2 := Container.Box(inner = CondNum.Number(val = 42))
    m3 := Container.Box(inner = CondNum.Number(val = 99))
    error += if cond_are_equal(m1, m2) { 0 } else { 1 }
    error += if cond_are_equal(m1, m3) { 1 } else { 0 }
    // are_equal with Pair[Int]
    error += if cond_are_equal(ia, ib) { 0 } else { 1 }
    // are_equal with plain Number (non-conditional)
    error += if cond_are_equal(CondNum.Number(val = 5), CondNum.Number(val = 5)) { 0 } else { 1 }
    error += if cond_are_equal(CondNum.Number(val = 5), CondNum.Number(val = 6)) { 1 } else { 0 }

    // sum_two with Pair[Number] and Pair[Int] (different conditional instantiations)
    sa := cond_sum_two(a, c)
    error += if sa.fst.val == 4 { 0 } else { 1 }
    si := cond_sum_two(ia, ic)
    error += if si.fst == 40 { 0 } else { 1 }

    error
}

// === test_trait_declaration ===

// Simple trait with one function
Comparable[T]: {
    compare(a: T, b: T) -> Bool
}

// Trait with multiple functions
Printable[T]: {
    to_string(a: T) -> CString
    print    (a: T) -> CInt
}

// Trait with inheritance (single parent)
Sortable[T]: Comparable[T] {
    rank(a: T, b: T) -> CInt
}

// Combined trait with parents and empty body
Displayable[T]: Comparable[T], Printable[T] { }

test_trait_declaration() { 0 }

// === test_trait_diamond_inheritance ===

// A is the base trait
// B and C both inherit from Eq
// D inherits from both B and C — diamond pattern
DiamondB[T]: Eq[T] {
    diamond_foo(a: T) -> T
}

DiamondC[T]: Eq[T] {
    diamond_bar(a: T) -> T
}

DiamondD[T]: DiamondB[T], DiamondC[T] { }

test_trait_diamond_inheritance() { 0 }

// === test_trait_inheritance_conformance ===

// User-defined combined trait: requires Add, Sub, Mul, Div
Numeric[T]: Add[T], Sub[T], Mul[T], Div[T] { }

// Function using the combined trait
inherit_eval[T: Numeric](a: T, b: T) -> T {
    a + b - a * b
}

test_trait_inheritance_conformance() {
    error := 0

    a := InheritNum.Number(val = 3)
    b := InheritNum.Number(val = 2)

    // Number satisfies Numeric (has add, sub, mul, div)
    r := inherit_eval(a, b)
    // 3 + 2 - 3 * 2 = 5 - 6 = -1
    error += if r.val == 0 - 1 {0} else {1}

    error
}

// === test_trait_ord_derives_eq ===

ord_is_sorted[T: Ord](a: T, b: T) -> Bool {
    a <= b
}

ord_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

test_trait_ord_derives_eq() {
    error := 0

    a := Foo.Thing(val = 1)
    b := Foo.Thing(val = 2)
    c := Foo.Thing(val = 1)

    // Ord bound works with only cmp
    error += if ord_is_sorted(a, b) { 0 } else { 1 }
    error += if ord_is_sorted(b, a) { 1 } else { 0 }

    // Eq bound also works — eq derived from cmp
    error += if ord_are_equal(a, c) { 0 } else { 1 }
    error += if ord_are_equal(a, b) { 1 } else { 0 }

    // != through Eq bound also works
    error += if a != b { 0 } else { 1 }
    error += if a != c { 1 } else { 0 }

    error
}

// === test_trait_ptr_receiver_builtin_int ===

ptr_modify[T: Mutate](x: Ptr[T]) -> CInt { x.mutate() }

test_trait_ptr_receiver_builtin_int() {
    a: Int := 10
    error  := if ptr_modify(a.&) == 1 { 0 } else { 1 }
    error += if a == 11 { 0 } else { 1 }
    error
}

// === main ===

main() {
    error := 0
    error += test_trait_conditional()
    error += test_trait_declaration()
    error += test_trait_diamond_inheritance()
    error += test_trait_inheritance_conformance()
    error += test_trait_ord_derives_eq()
    error += test_trait_ptr_receiver_builtin_int()
    error
}
