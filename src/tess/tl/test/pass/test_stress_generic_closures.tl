#module main

// A simple struct for testing generic closures with struct types
Pair: { a: Int, b: Int }

pair_sum(p: Pair) -> Int { p.a + p.b }

// --- Generic functions that accept closures ---

// Apply a closure to a value (generic in both T and result)
apply_to[T](val: T, f: (T) -> Int) -> Int { f(val) }

// Apply a zero-arg closure generically
run_thunk[T](f: () -> T) -> T { f() }

// Apply two closures and combine results
combine[T](val: T, f: (T) -> Int, g: (T) -> Int) -> Int {
    f(val) + g(val)
}

// Generic identity - used to test closure calling generic fn
identity[T](x: T) -> T { x }

// Generic function with two type params
map_pair[A, B](val: A, f: (A) -> B) -> B { f(val) }

main() {
    error := 0

    // Scenario 1: Generic apply_to specialized at multiple types
    ({
        // Int specialization
        r1 := apply_to(42, (x: Int) { x + 1 })
        error += if r1 == 43 { 0 } else { 1 }

        // Bool specialization
        r2 := apply_to(true, (b: Bool) { if b { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }

        // Struct specialization
        p := Pair(a = 10, b = 20)
        r3 := apply_to(p, (pp: Pair) { pp.a + pp.b })
        error += if r3 == 30 { 0 } else { 1 }
        void
    })

    // Scenario 2: Closure capturing a generic value, passed to generic function
    ({
        val := 100
        f := () { val }
        result := run_thunk(f)
        error += if result == 100 { 0 } else { 1 }

        bval := true
        g := () { bval }
        result_b := run_thunk(g)
        error += if result_b == true { 0 } else { 1 }
        void
    })

    // Scenario 3: Nested generic closures - closure calls another generic fn
    ({
        x := 77
        f := (v: Int) { identity(v) + 1 }
        result := apply_to(x, f)
        error += if result == 78 { 0 } else { 1 }

        p := Pair(a = 3, b = 4)
        g := (pp: Pair) { identity(pp.a) + identity(pp.b) }
        result2 := apply_to(p, g)
        error += if result2 == 7 { 0 } else { 1 }
        void
    })

    // Scenario 4: Shadowed variable names inside closures with generic context
    ({
        x := 10
        f := () { x }       // captures original x = 10
        x := 20              // shadow: new binding
        g := () { x }       // captures shadowed x = 20
        r1 := run_thunk(f)
        r2 := run_thunk(g)
        error += if r1 == 10 { 0 } else { 1 }
        error += if r2 == 20 { 0 } else { 1 }

        // Same pattern with generic apply_to
        y := 100
        result := apply_to(5, (v: Int) { v + y })
        y := 200  // shadow
        result2 := apply_to(5, (v: Int) { v + y })
        error += if result == 105 { 0 } else { 1 }
        error += if result2 == 205 { 0 } else { 1 }
        void
    })

    // Scenario 5: Closure that mutates a captured variable, used with generics
    ({
        counter := 0
        inc := (n: Int) {
            counter = counter + n
            counter
        }
        r1 := apply_to(10, inc)
        r2 := apply_to(20, inc)
        r3 := apply_to(30, inc)
        error += if r3 == 60 { 0 } else { 1 }
        error += if counter == 60 { 0 } else { 1 }
        void
    })

    // Scenario 6: combine - two closures on same generic value
    ({
        p := Pair(a = 3, b = 7)
        result := combine(p, (pp: Pair) { pp.a }, (pp: Pair) { pp.b })
        error += if result == 10 { 0 } else { 1 }

        result2 := combine(42, (x: Int) { x + 1 }, (x: Int) { x * 2 })
        error += if result2 == 127 { 0 } else { 1 }
        void
    })

    // Scenario 7: map_pair with two type params
    ({
        r1 := map_pair(42, (x: Int) { x > 0 })
        error += if r1 == true { 0 } else { 1 }

        r2 := map_pair(true, (b: Bool) { if b { 1 } else { 0 } })
        error += if r2 == 1 { 0 } else { 1 }
        void
    })

    error
}
