#module main
#import <stdio.tl>

main() {
  error := 0

  // Test scientific notation with negative exponent
  f1 := 1e-2
  error = error + if f1 == 0.01 {0} else {1}

  f2 := 1.5e-3
  error = error + if f2 == 0.0015 {0} else {1}

  f3 := 2.5E-1
  error = error + if f3 == 0.25 {0} else {1}

  // Test scientific notation with explicit positive exponent
  f4 := 1e+2
  error = error + if f4 == 100.0 {0} else {1}

  f5 := 1.5e+3
  error = error + if f5 == 1500.0 {0} else {1}

  f6 := 2.5E+1
  error = error + if f6 == 25.0 {0} else {1}

  // Test integer with negative exponent (becomes float)
  f7 := 5e-1
  error = error + if f7 == 0.5 {0} else {1}

  // Test hex numbers still work (e is a hex digit)
  h1 := 0xABCDef
  error = error + if h1 == 11259375 {0} else {1}

  h2 := 0xE0
  error = error + if h2 == 224 {0} else {1}

  c_printf("number formats test: %d errors\n", error)
  error
}
