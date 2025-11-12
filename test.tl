#module main
#import <stdlib.tl>

main() {
  p : Ptr(Int) := c_malloc(8 * sizeof(Int))
  if p == null {_tl_fatal_("oom")}
  i := 1
  p[i+2] = 0
  p[3]
}
