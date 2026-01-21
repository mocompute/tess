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

  // Test binary with lowercase b
  error = error + if 0b1010 == 10 {0} else {1}
  error = error + if 0b11111111 == 255 {0} else {1}
  error = error + if 0b0 == 0 {0} else {1}
  error = error + if 0b1 == 1 {0} else {1}

  // Test binary with uppercase B
  error = error + if 0B1010 == 10 {0} else {1}
  error = error + if 0B11111111 == 255 {0} else {1}

  // Test binary with underscores
  error = error + if 0b1111_0000 == 240 {0} else {1}
  error = error + if 0b1010_1010 == 170 {0} else {1}

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
