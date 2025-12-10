#module main
#import <Alloc.tl>

main() {

  error := 0

  p1 : Ptr(Int) := Alloc.transient(16 * sizeof(Int))
  p1[15] = 456
  p1[0]  = 123

  error = error + if p1[15] - p1[0] == 333 {0} else {1}

  error
}
