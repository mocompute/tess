// Stress test: module-scoped generic tagged unions exercising all three
// constructor paths (unscoped, scoped, named-field) with generics and modules.

#module A

Outcome[a]: | Good { v: a }
            | Bad { code: Int }
            | Vacant

unwrap_or[a](r: Outcome[a], default: a) -> a {
    when r {
        g: Good { g.v }
        b: Bad { default }
        n: Vacant { default }
    }
}

is_good[a](r: Outcome[a]) -> Bool {
    when r {
        g: Good { true }
        b: Bad { false }
        n: Vacant { false }
    }
}

#module B

Pair[a]: | Both { fst: a, snd: a }
         | Single { only: a }
         | Neither

first_or[a](p: Pair[a], default: a) -> a {
    when p {
        b: Both { b.fst }
        s: Single { s.only }
        n: Neither { default }
    }
}

// Generic function that takes an Outcome from module A
check_outcome[a](r: A.Outcome[a], fallback: a) -> a {
    A.unwrap_or(r, fallback)
}

#module main

main() {
    error := 0

    // === Module A: Outcome[Int] via constructor paths ===

    // Path 1: Module-scoped positional: A.Good(value)
    r1 := A.Good(42)
    error += if A.unwrap_or(r1, 0) == 42 { 0 } else { 1 }

    // Path 2: Module-scoped with type path: A.Outcome.Good(value)
    r2 := A.Outcome.Good(99)
    error += if A.unwrap_or(r2, 0) == 99 { 0 } else { 1 }

    // Path 3: Module-scoped named fields: A.Good(v = value)
    r3 := A.Good(v = 7)
    error += if A.unwrap_or(r3, 0) == 7 { 0 } else { 1 }

    // Named fields via module path: A.Bad(code = 404)
    r4: A.Outcome[Int] := A.Bad(code = 404)
    error += if A.is_good(r4) { 1 } else { 0 }

    // === Nullary variant: Vacant ===

    // Scoped nullary: A.Vacant (no parens, auto-invoked)
    r5: A.Outcome[Int] := A.Vacant
    error += if A.unwrap_or(r5, -1) == -1 { 0 } else { 1 }

    // Scoped nullary via type path: A.Outcome.Vacant
    r6: A.Outcome[Int] := A.Outcome.Vacant
    error += if A.is_good(r6) { 1 } else { 0 }

    // === Outcome[Bool] — different specialization ===

    rb1 := A.Good(true)
    error += if A.unwrap_or(rb1, false) { 0 } else { 1 }

    rb2 := A.Outcome.Good(false)
    error += if A.unwrap_or(rb2, true) { 1 } else { 0 }

    // === Module B: Pair[Int] ===

    p1 := B.Both(fst = 10, snd = 20)
    error += if B.first_or(p1, 0) == 10 { 0 } else { 1 }

    p2 := B.Single(only = 77)
    error += if B.first_or(p2, 0) == 77 { 0 } else { 1 }

    p3: B.Pair[Int] := B.Neither
    error += if B.first_or(p3, -1) == -1 { 0 } else { 1 }

    // Pair[Bool]
    pb := B.Both(fst = true, snd = false)
    error += if B.first_or(pb, false) { 0 } else { 1 }

    // === Pattern matching on module-scoped generic tagged unions ===

    val := A.Good(100)
    matched := when val {
        g: Good { g.v }
        b: Bad { b.code }
        n: Vacant { -1 }
    }
    error += if matched == 100 { 0 } else { 1 }

    val2: A.Outcome[Int] := A.Bad(code = 500)
    matched2 := when val2 {
        g: Good { g.v }
        b: Bad { b.code }
        n: Vacant { -1 }
    }
    error += if matched2 == 500 { 0 } else { 1 }

    // === Cross-module generic function: B.check_outcome uses A.Outcome ===

    cr := A.Good(55)
    error += if B.check_outcome(cr, 0) == 55 { 0 } else { 1 }

    cr2: A.Outcome[Int] := A.Vacant
    error += if B.check_outcome(cr2, -1) == -1 { 0 } else { 1 }

    // === Multiple specializations in same function ===

    oi := A.Good(10)
    ob := A.Good(true)
    error += if A.unwrap_or(oi, 0) == 10 { 0 } else { 1 }
    error += if A.unwrap_or(ob, false) { 0 } else { 1 }

    error
}
