// Test auto-collapsed generic types passed between modules.
// The type must be consistent when created in one module and
// consumed by functions in the defining module.

#module Pair

T[a]: { fst: a, snd: a }

swap(p) {
    T(fst = p.snd, snd = p.fst)
}

both(p, f) {
    T(fst = f(p.fst), snd = f(p.snd))
}

#module Util

// A function in a different module that takes and returns Pair
double_pair(p: Pair[Int]) -> Pair[Int] {
    Pair.both(p, (x) { x * 2 })
}

#module main

main() {
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
