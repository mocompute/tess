#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
  error := 0

  // Test bitwise AND (&)
  // 12 = 0b1100, 10 = 0b1010, result = 0b1000 = 8
  error = error + if (12 & 10) == 8 {0} else {1}
  error = error + if (0xFF & 0x0F) == 0x0F {0} else {1}

  // Test bitwise OR (|)
  // 12 = 0b1100, 10 = 0b1010, result = 0b1110 = 14
  error = error + if (12 | 10) == 14 {0} else {1}
  error = error + if (0xF0 | 0x0F) == 0xFF {0} else {1}

  // Test bitwise XOR (^)
  // 12 = 0b1100, 10 = 0b1010, result = 0b0110 = 6
  error = error + if (12 ^ 10) == 6 {0} else {1}
  error = error + if (0xFF ^ 0xFF) == 0 {0} else {1}

  // Test bitwise NOT (~) - note: inverts all bits including sign
  x : CUInt8 := 0
  y := ~x
  error = error + if y == 0xFF {0} else {1}

  // Test left shift (<<)
  error = error + if (1 << 4) == 16 {0} else {1}
  // 1 = 0b0001, result = 0b1000 = 8
  error = error + if (1 << 3) == 8 {0} else {1}

  // Test right shift (>>)
  error = error + if (16 >> 4) == 1 {0} else {1}
  // 8 = 0b1000, result = 0b0001 = 1
  error = error + if (8 >> 3) == 1 {0} else {1}

  // Test combinations
  // (15 & 10) | 4 = 10 | 4 = 14
  error = error + if ((15 & 10) | 4) == 14 {0} else {1}
  error = error + if ((1 << 8) - 1) == 255 {0} else {1}

  // Test with different integer types
  a : CInt := 0x12345678
  b : CInt := 0x0F0F0F0F
  error = error + if (a & b) == 0x02040608 {0} else {1}
  error = error + if (a | b) == 0x1F3F5F7F {0} else {1}

  c_printf("bitwise operators test: %d errors\n", error)
  error
}
