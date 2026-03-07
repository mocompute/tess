#module main

// Helper functions for documentation examples
setup             ()     { void  }
compute           ()     { 10    }
cleanup           ()     { void  }
calculate_discount()     { 5     }
apply_tax         (x)    { x     }
add               (a, b) { a + b }

main() {
    error := 0

    // Test basic block expression (from documentation)
    result := ( {
               setup()
               compute()
               cleanup()
               42
               })
    error += if result == 42 { 0 } else { 1 }

    // Test block expression in arithmetic context (from documentation)
    base_cost := 100
    price     := 50
    total     := base_cost + ( {
                          discount := calculate_discount()
                          apply_tax(price - discount)
                          })
    // 100 + (50 - 5) = 145
    error += if total == 145 { 0 } else { 1 }

    // Test block expression with let-in bindings (from documentation)
    value := ( {
              x := 10
              y := 20
              x * y
              })
    error += if value == 200 { 0 } else { 1 }

    // Test nested block expressions
    nested := ( {
               a := ( {
               b := 5
               b * 2
               })
               a + 3
               })
    error += if nested == 13 { 0 } else { 1 }

    // Test block expression as function argument
    sum := add(( { 1 + 2 }), ( { 3 + 4 }))
    error += if sum == 10 { 0 } else { 1 }

    // Test empty block (should return void, but we test with a value)
    empty_result := ( { 0 })
    error += if empty_result == 0 { 0 } else { 1 }

    // Test block with multiple statements
    multi := ( {
              a := 1
              b := 2
              c := 3
              a + b + c
              })
    error += if multi == 6 { 0 } else { 1 }

    error
}
