#module main
#import <stdlib.tl>

main() {
  // we automatically decay array to pointer, unlike C
  p := CArray(Int, 16)
  p[0] = 123

  // this gives a Int**, unlike C
  pp := p.&

  // double dereference
  val := pp.*.*
  val - 123
}
