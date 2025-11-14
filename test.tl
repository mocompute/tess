#module main
#import <stdio.tl>

main() {
  n := 12
  mask := 8 - 1
  res := (n + mask) & ~mask
  // c_printf("res = %li\n", res)
  0
}
