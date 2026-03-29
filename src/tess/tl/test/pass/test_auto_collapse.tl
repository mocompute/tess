// Combined auto-collapse tests.
// Tests: T-only, same-name-only, operator overload, generic fn,
//        cross-module, tagged union, UFCS dispatch.

#module Baz

T: { y: Int }

#module Bar

Bar: { x: Int }

#module Point

T[a]: { x: a, y: a }

add(p1, p2) {
    T(x = p1.x + p2.x, y = p1.y + p2.y)
}

eq(p1, p2) {
    p1.x == p2.x && p1.y == p2.y
}

scale(p, factor) {
    T(x = p.x * factor, y = p.y * factor)
}

map(p, f) {
    T(x = f(p.x), y = f(p.y))
}

dot(p1, p2) {
    p1.x * p2.x + p1.y * p2.y
}

#module Pair

T[a]: { fst: a, snd: a }

swap(p) {
    T(fst = p.snd, snd = p.fst)
}

both(p, f) {
    T(fst = f(p.fst), snd = f(p.snd))
}

#module Util

double_pair(p: Pair[Int]) -> Pair[Int] {
    Pair.both(p, (x) { x * 2 })
}

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

test_t_only() {
    b := Baz(y = 7)
    if b.y == 7 { 0 } else { 1 }
}

test_same_name_only() {
    b := Bar(x = 42)
    if b.x == 42 { 0 } else { 1 }
}

test_operator_overload() {
    a := Point(x = 1, y = 2)
    b := Point(x = 3, y = 4)
    c := a + b
    c_printf(c"(%d, %d)\n", c.x, c.y)

    if a != b { c_printf(c"different\n") }

    af := Point(x = 1.2, y = 2.3)
    bf := Point(x = 3.4, y = 4.5)
    cf := af + bf
    c_printf(c"(%f, %f)\n", cf.x, cf.y)
    0
}

test_generic_fn() {
    error := 0

    // Direct call through auto-collapsed alias
    a := Point(x = 3, y = 4)
    b := Point.scale(a, 2)
    error += if b.x == 6 && b.y == 8 { 0 } else { 1 }

    // Assign result back to same type
    c := Point.scale(b, 3)
    error += if c.x == 18 && c.y == 24 { 0 } else { 1 }

    // map: returns a different specialization (Int -> Int via lambda)
    d := Point.map(a, (x) { x + 10 })
    error += if d.x == 13 && d.y == 14 { 0 } else { 1 }

    // Float variant
    af := Point(x = 1.5, y = 2.5)
    bf := Point.scale(af, 2.0)
    error += if bf.x == 3.0 && bf.y == 5.0 { 0 } else { 1 }

    error
}

test_cross_module() {
    error := 0

    p := Pair(fst = 10, snd = 20)

    // Pass auto-collapsed type to defining module's function
    q := Pair.swap(p)
    error += if q.fst == 20 && q.snd == 10 { 0 } else { 1 }

    // Pass through a third module
    r := Util.double_pair(p)
    error += if r.fst == 20 && r.snd == 40 { 0 } else { 1 }

    // Chain: create, pass to module fn, pass to another module fn
    s := Pair.swap(Util.double_pair(p))
    error += if s.fst == 40 && s.snd == 20 { 0 } else { 1 }

    error
}

test_tagged_union() {
    error := 0

    // Use auto-collapsed alias "Opt" for "Opt.T"
    a           := Opt.Has(42)
    b: Opt[Int] := Opt.Empty

    // Call generic function on auto-collapsed type
    error += if Opt.my_unwrap(a, 0) == 42 { 0 } else { 1 }
    error += if Opt.my_unwrap(b, 99) == 99 { 0 } else { 1 }

    // Float variant
    af := Opt.Has(3.14)
    error += if Opt.my_unwrap(af, 0.0) == 3.14 { 0 } else { 1 }

    error
}

test_ufcs() {
    error := 0

    a := Point(x = 3, y = 4)

    // Implicit module resolve: a.scale(2) rewrites to Point.scale(a, 2)
    b := a.scale(2)
    error += if b.x == 6 && b.y == 8 { 0 } else { 1 }

    // Implicit module chaining
    c := a.scale(2).scale(3)
    error += if c.x == 18 && c.y == 24 { 0 } else { 1 }

    // Implicit module returning a scalar
    d := a.dot(a)
    error += if d == 25 { 0 } else { 1 }

    // Explicit .Point. still works
    e := a.Point.scale(2)
    error += if e.x == 6 && e.y == 8 { 0 } else { 1 }

    // Float variant with implicit module
    af := Point(x = 1.5, y = 2.5)
    bf := af.scale(2.0)
    error += if bf.x == 3.0 && bf.y == 5.0 { 0 } else { 1 }

    error
}

main() {
    error := 0
    error += test_t_only()
    error += test_same_name_only()
    error += test_operator_overload()
    error += test_generic_fn()
    error += test_cross_module()
    error += test_tagged_union()
    error += test_ufcs()
    error
}
