#module main
#import <stdio.tl>

main() {
  error := 0

  // Test hexadecimal with lowercase x
  error = error + if 0x10 == 16 {0} else {1}
  error = error + if 0xff == 255 {0} else {1}
  error = error + if 0xABCD == 43981 {0} else {1}

  // Test hexadecimal with uppercase X
  error = error + if 0X10 == 16 {0} else {1}
  error = error + if 0XFF == 255 {0} else {1}

  // Test octal (leading zero)
  error = error + if 010 == 8 {0} else {1}
  error = error + if 0377 == 255 {0} else {1}
  error = error + if 0644 == 420 {0} else {1}

  // Test decimal
  error = error + if 42 == 42 {0} else {1}
  error = error + if 1000000 == 1000000 {0} else {1}

  // Test with underscores as separators
  error = error + if 1_000_000 == 1000000 {0} else {1}
  error = error + if 0xFF_FF == 0xFFFF {0} else {1}

  // Test negative values
  error = error + if -0x10 == -16 {0} else {1}
  error = error + if -010 == -8 {0} else {1}

  // Test mixed comparison
  error = error + if 0x100 == 256 {0} else {1}
  error = error + if 0400 == 256 {0} else {1}

  c_printf("integer literals test: %d errors\n", error)
  error
}
