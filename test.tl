#import "src/tl/std/stdio.tl"

#module main

main() {
  i := 0
  len := 10
  while i < len, i = i + 1 {
    if i % 2 == 0 { continue }
    c_printf("%i\n", i)
  }
  0
}
