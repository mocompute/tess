#module main
#import <stdio.tl>

main() {
  // we automatically decay array to pointer, unlike C
  data := 0x0102030405060708
  p : Ptr(Int) := CArray(Int, 16) // p's annotation is not required
  p[0] = data

  // cast pointer to a different type
  b : Ptr(CUnsignedChar) := p
  c_printf("b[0] = 0x%x\n", b[0])
  c_printf("b[1] = 0x%x\n", b[1])
  c_printf("b[2] = 0x%x\n", b[2])

  // this gives a Int**, unlike C
  pp := p.&

  // double dereference
  val := pp.*.*
  error := val - data

  // Note: assumes little-endian
  error = error + if (0x08 == b[0]) {0} else {1}
  error = error + if (0x07 == b[1]) {0} else {1}
  error = error + if (0x06 == b[2]) {0} else {1}
  error
}
