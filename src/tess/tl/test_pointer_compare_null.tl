#module main
#import <stdlib.tl>

main() {
  p : Ptr(CInt) := c_malloc(1024)
  if p == null { 1 } else { 0 }
}
