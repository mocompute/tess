#module main

// 5+ levels of shadowing with := across nested blocks
test_deep_shadowing() {
    x := 1
    x := x + 1                          // x = 2
    x := x + 1                          // x = 3
    x := x + 1                          // x = 4
    x := x + 1                          // x = 5
    x := x + 1                          // x = 6
    if x == 6 {0} else {1}
}

// Shadowing inside nested blocks
test_block_shadowing() {
    x      := 10
    result := ( {
               x     := 20
               inner := ( {
               x     := 30
               x
               })
               x + inner
               })
    // result = 20 + 30 = 50, outer x still 10
    error := 0
    error += if result == 50 {0} else {1}
    error += if x == 10 {0} else {1}
    error
}

Shape: | Circle { radius: Int }
       | Square { length: Int }

// Shadowing inside when arms (same name in different arms)
test_when_arm_shadowing() {
    s1 := Circle(5)
    s2 := Square(10)

    r1 := when s1 {
        x: Circle { x.radius }
        x: Square { x.length }
    }

    r2 := when s2 {
        x: Circle { x.radius }
        x: Square { x.length }
    }

    error := 0
    error += if r1 == 5 {0} else {1}
    error += if r2 == 10 {0} else {1}
    error
}

// Shadowing with same name in nested if/else branches
test_if_else_shadowing() {
    x := 100

    result := if 1 == 1 {
        x := 200
        x
    } else {
        x := 300
        x
    }

    error := 0
    error += if result == 200 {0} else {1}
    error += if x == 100 {0} else {1}   // outer x unchanged
    error
}

// Rebinding loop counter-like variable inside loop body
test_loop_rebind() {
    sum := 0
    i   := 0
    while i < 5, i = i + 1 {
        // Shadow i inside the loop body
        i := i * 10
        sum = sum + i
    }
    // Outer i goes 0,1,2,3,4
    // Inner shadow: 0,10,20,30,40 => sum = 100
    if sum == 100 {0} else {1}
}

// Helper to call a closure
apply(f: () -> Int) -> Int { f() }

main() {
    error := 0
    error += test_deep_shadowing()
    error += test_block_shadowing()
    error += test_when_arm_shadowing()
    error += test_if_else_shadowing()
    error += test_loop_rebind()

    // Test: Shadowing inside lambda captures, then mutating the outer
    // (closures must be defined in main)
    ( {
     x := 10
     f := () { x }
     x := 20                            // shadow x (new binding)
     g := () { x }
     error += if f() == 10 {0} else {1}
     error += if g() == 20 {0} else {1}
     void
     })

    // Test: Multiple shadows in nested function calls
    ( {
     x := 1
     f := () {
     x := 2
     x
     }
     result := apply(f)
     error += if result == 2 {0} else {1}
     error += if x == 1 {0} else {1}    // outer unchanged
     void
     })

    error
}
