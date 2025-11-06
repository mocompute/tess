#import "src/tl/std/stdlib.tl"
#import "src/tl/std/stdio.tl"

#module main

main() {
  x := c_rand()
  c_printf("rand = %i\n", x)
  x = c_rand()
  c_printf("rand = %i\n", x)
  false
}
