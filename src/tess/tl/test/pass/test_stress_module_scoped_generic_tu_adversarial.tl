// Adversarial stress test: module-scoped generic tagged unions with
// name collisions, nested types, and multiple specializations.

// Two modules with identically-named variants — must use module scope to disambiguate.

#module M1

Status[a]: | Ready { v: a }
           | Waiting
           | Done { v: a }

extract[a](s: Status[a], default: a) -> a {
    when s {
        r: Ready { r.v }
        w: Waiting { default }
        d: Done { d.v }
    }
}

#module M2

Status[a]: | Ready { v: a }
           | Waiting
           | Done { v: a }

extract[a](s: Status[a], default: a) -> a {
    when s {
        r: Ready { r.v }
        w: Waiting { default }
        d: Done { d.v }
    }
}

#module Nest

// A generic tagged union variant that contains ANOTHER module's generic tagged union
Wrap[a]: | Inner { payload: M1.Status[a] }
         | Leaf { v: a }
         | Nil

get_inner[a](w: Wrap[a], default: a) -> a {
    when w {
        i: Inner { M1.extract(i.payload, default) }
        l: Leaf { l.v }
        n: Nil { default }
    }
}

#module main

main() {
    error := 0

    // === Disambiguating identically-named variants via module scope ===

    // M1.Ready and M2.Ready are distinct types
    s1 := M1.Ready(42)
    s2 := M2.Ready(99)

    error += if M1.extract(s1, 0) == 42 { 0 } else { 1 }
    error += if M2.extract(s2, 0) == 99 { 0 } else { 1 }

    // M1.Done and M2.Done
    d1 := M1.Done(10)
    d2 := M2.Done(20)
    error += if M1.extract(d1, 0) == 10 { 0 } else { 1 }
    error += if M2.extract(d2, 0) == 20 { 0 } else { 1 }

    // Nullary variants with same name from different modules
    w1: M1.Status[Int] := M1.Waiting
    w2: M2.Status[Int] := M2.Waiting
    error += if M1.extract(w1, -1) == -1 { 0 } else { 1 }
    error += if M2.extract(w2, -2) == -2 { 0 } else { 1 }

    // === Type-path scoped construction with identically-named variants ===

    ts1 := M1.Status.Ready(77)
    ts2 := M2.Status.Ready(88)
    error += if M1.extract(ts1, 0) == 77 { 0 } else { 1 }
    error += if M2.extract(ts2, 0) == 88 { 0 } else { 1 }

    // === Named field construction with identically-named variants ===

    nf1 := M1.Ready(v = 33)
    nf2 := M2.Ready(v = 44)
    error += if M1.extract(nf1, 0) == 33 { 0 } else { 1 }
    error += if M2.extract(nf2, 0) == 44 { 0 } else { 1 }

    // === Nested: variant containing another module's generic tagged union ===

    inner_status := M1.Ready(100)
    wrapped      := Nest.Inner(payload = inner_status)
    error += if Nest.get_inner(wrapped, 0) == 100 { 0 } else { 1 }

    leaf := Nest.Leaf(55)
    error += if Nest.get_inner(leaf, 0) == 55 { 0 } else { 1 }

    nil_val: Nest.Wrap[Int] := Nest.Nil
    error += if Nest.get_inner(nil_val, -1) == -1 { 0 } else { 1 }

    // Nested with Bool specialization
    inner_bool   := M1.Ready(true)
    wrapped_bool := Nest.Inner(payload = inner_bool)
    error += if Nest.get_inner(wrapped_bool, false) { 0 } else { 1 }

    // === Multiple specializations in a single function ===

    // M1.Status[Int], M1.Status[Bool], M2.Status[Int], M2.Status[Bool] all in scope
    a1 := M1.Ready(1)
    a2 := M1.Ready(true)
    a3 := M2.Ready(2)
    a4 := M2.Ready(false)

    error += if M1.extract(a1, 0) == 1 { 0 } else { 1 }
    error += if M1.extract(a2, false) { 0 } else { 1 }
    error += if M2.extract(a3, 0) == 2 { 0 } else { 1 }
    error += if M2.extract(a4, true) { 1 } else { 0 }

    // === Pattern matching on cross-module values ===

    pm1     := M1.Done(v = 200)
    result1 := when pm1 {
        r: Ready { r.v }
        w: Waiting { -1 }
        d: Done { d.v }
    }
    error += if result1 == 200 { 0 } else { 1 }

    pm2     := M2.Status.Done(300)
    result2 := when pm2 {
        r: Ready { r.v }
        w: Waiting { -1 }
        d: Done { d.v }
    }
    error += if result2 == 300 { 0 } else { 1 }

    // Nested: pattern match on Wrap, then inner pattern match on Status
    deep    := Nest.Inner(payload = M1.Done(999))
    result3 := when deep {
        i: Inner {
            when i.payload {
                r: Ready { r.v }
                w: Waiting { -1 }
                d: Done { d.v }
            }
        }
        l: Leaf { l.v }
        n: Nil { -1 }
    }
    error += if result3 == 999 { 0 } else { 1 }

    error
}
