#module main
#import <stdio.tl>
#import <stdlib.tl>

foo() { c_printf("goodbye.\n") }

main() {
  c_atexit(foo/0)
  0
}
