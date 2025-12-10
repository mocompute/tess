#module main
#import <stdio.tl>

main() {
  // we automatically decay array to pointer, unlike C
  data := 0x0102030405060708
  p := CArray(Int, 16)
  p[0] = data

  // cast pointer to a different type
  b : Ptr(CUnsignedChar) := p
  c_printf("b[0] = 0x%x\n", b[0])
  c_printf("b[1] = 0x%x\n", b[1])

  // this gives a Int**, unlike C
  pp := p.&

  // double dereference
  val := pp.*.*
  val - data

  // FIXME: this test does not test pointer cast because it needs endian detection first
}
