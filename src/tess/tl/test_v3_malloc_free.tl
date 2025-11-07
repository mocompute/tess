#module main
#import <stdlib.tl>

main() {
  p := c_malloc(64)
  c_free(p)
  0
}
