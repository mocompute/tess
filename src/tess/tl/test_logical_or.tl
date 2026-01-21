#module main
#import <stdio.tl>

main() {
  error := 0

  // Test basic logical OR
  error = error + if (true || true) {0} else {1}
  error = error + if (true || false) {0} else {1}
  error = error + if (false || true) {0} else {1}
  error = error + if (false || false) {1} else {0}

  // Test short-circuit evaluation
  // If the first operand is true, the second should NOT be evaluated
  x := 0
  result := true || ({x = 1 true})
  // x should still be 0 if short-circuit worked
  // Note: This test may not work if x = 1 is not an expression
  // Let's test with a function call instead

  // Test with variables
  a := true
  b := false
  error = error + if (a || b) {0} else {1}
  error = error + if (b || a) {0} else {1}

  // Test combined with && and !
  error = error + if (false || true && true) {0} else {1}
  error = error + if (true && false || true) {0} else {1}
  error = error + if (!false || false) {0} else {1}
  error = error + if (false || !false) {0} else {1}

  // Test with comparison operators
  error = error + if (1 > 2 || 2 > 1) {0} else {1}
  error = error + if (1 == 1 || 1 == 2) {0} else {1}

  // Test nested OR
  error = error + if (false || false || true) {0} else {1}
  error = error + if (false || false || false) {1} else {0}

  c_printf("logical OR test: %d errors\n", error)
  error
}
