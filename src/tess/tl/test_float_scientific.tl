#module main
#import <stdio.tl>

main() {
  error := 0

  // Test basic scientific notation (positive exponent)
  f1 := 1.5e10
  error += if f1 == 15000000000.0 {0} else {1}

  // Test uppercase E (positive exponent)
  f2 := 2.5E3
  error += if f2 == 2500.0 {0} else {1}

  // Test with integer coefficient
  f3 := 1e6
  error += if f3 == 1000000.0 {0} else {1}

  // Test negative coefficient with positive exponent
  f4 := -3.14e2
  error += if f4 == -314.0 {0} else {1}

  // Test Avogadro's number
  f5 := 6.022e23
  error += if f5 > 0.0 {0} else {1}

  // Test small exponent
  f6 := 1.234e1
  error += if f6 == 12.34 {0} else {1}

  c_printf("float scientific notation test: %d errors\n", error)
  error
}
