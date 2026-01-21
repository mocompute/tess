#module main
#import <stdio.tl>
#import <stdint.tl>

// Simple struct for testing
Point : { x: Int, y: Int }

main() {
  error := 0

  // Test alignof with primitive types
  // CChar typically has alignment of 1
  error = error + if alignof(CChar) >= 1 {0} else {1}

  // Int typically has alignment >= 4
  error = error + if alignof(Int) >= 4 {0} else {1}

  // Pointer alignment (typically 4 on 32-bit, 8 on 64-bit)
  error = error + if alignof(Ptr(Int)) >= 4 {0} else {1}

  // Test alignof with struct type
  // Struct alignment should be at least as large as its largest member
  error = error + if alignof(Point) >= alignof(Int) {0} else {1}

  // Test that sizeof is always >= alignof
  error = error + if sizeof(Int) >= alignof(Int) {0} else {1}
  error = error + if sizeof(Point) >= alignof(Point) {0} else {1}
  error = error + if sizeof(Ptr(Void)) >= alignof(Ptr(Void)) {0} else {1}

  // Test alignof with fixed-width types
  error = error + if alignof(CInt8) >= 1 {0} else {1}
  error = error + if alignof(CInt16) >= 2 {0} else {1}
  error = error + if alignof(CInt32) >= 4 {0} else {1}
  error = error + if alignof(CInt64) >= 4 {0} else {1}  // 8 on most 64-bit platforms

  // Test alignof on a value (should give the same as the type)
  p := Point(x = 1, y = 2)
  error = error + if alignof(p) == alignof(Point) {0} else {1}

  c_printf("alignof test: %d errors\n", error)
  error
}
