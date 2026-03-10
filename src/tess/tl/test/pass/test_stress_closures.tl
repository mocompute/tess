#module main

Shape: | Circle { radius: Int }
       | Square { length: Int }

// Passing a closure to a function that calls it inside a when arm
call_in_when(s: Shape, f: () -> Int) -> Int {
    when s {
        c:  Circle { f() + c.radius }
        sq: Square { f() + sq.length }
    }
}

// Helper to call a closure
apply(f: () -> Int) -> Int { f() }

main() {
    error := 0

    // Test 1: Lambda capturing a variable that is later shadowed
    ( {
     x := 10
     f := () { x }
     x := 20                            // shadow x (new binding)
     g := () { x }
     error += if f() == 10 {0} else {1}
     error += if g() == 20 {0} else {1}
     void
     })

    // Test 2: Lambda capturing a when-bound variable
    ( {
     s      := Circle(7)
     result := when s {
     c: Circle {
     val := c.radius
     f   := () { val }
     f() * 2
     }
     sq: Square { 0 }
     }
     error += if result == 14 {0} else {1}
     void
     })

    // Test 3: Multiple lambdas capturing the same mutable variable
    ( {
     counter := 0
     inc     := () { counter = counter + 1 }
     get     := () { counter }
     inc()
     inc()
     inc()
     error += if get() == 3 {0} else {1}
     void
     })

    // Test 4: Passing a closure to a function that calls it inside a when arm
    ( {
     x      := 100
     f      := () { x }
     s      := Circle(5)
     result := call_in_when(s, f)
     error += if result == 105 {0} else {1}
     void
     })

    // Test 5: Closure that mutates captured variable, called multiple times
    ( {
     total := 0
     add   := (n: Int) { total = total + n }
     add(10)
     add(20)
     add(30)
     error += if total == 60 {0} else {1}
     void
     })

    // Test 6: Nested closures - closure inside a closure
    ( {
     x     := 5
     outer := () {
     y     := x + 10
     inner := () { y }
     inner()
     }
     result := apply(outer)
     error += if result == 15 {0} else {1}
     void
     })

    // Test 7: Closure captured before a loop, sees mutations after loop
    ( {
     sum := 0
     f   := () { sum }
     i   := 0
     while i < 5, i = i + 1 {
     sum            = sum + i
     }
        // f captures sum by reference, so should see mutations
     error += if f() == 10 {0} else {1}
     void
     })

    error
}
