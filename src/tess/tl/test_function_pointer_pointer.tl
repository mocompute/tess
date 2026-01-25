#module main
#import <stdlib.tl>

// Test: Pointer to function pointer (Ptr(Ptr(...)))
// This feature is not currently implemented correctly.

negate(x: Int) -> Int { 0 - x }
add_one(x: Int) -> Int { x + 1 }

main() {
  error := 0

  // Create pointer to function pointer
  fp := negate/1
  pp := fp.&

  // Dereference and invoke: negate(5) = -5
  result1 := pp.*(5)
  error += if result1 == -5 {0} else {1}

  // Mutate through pointer to pointer
  pp.* = add_one/1
  result2 := fp(10)  // fp now points to add_one
  error += if result2 == 11 {0} else {1}

  error
}
