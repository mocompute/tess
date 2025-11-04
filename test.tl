#import "src/tl/std/stdlib.tl"

#module main

main() {
  out : String := ()
  d := c_strtod("1.234 foo bar", out.&)
  c_printf("out = %p\n", out.&)
  c_printf("out = '%s'\n", out)
  c_printf("d = %f\n", d)
  0
}
