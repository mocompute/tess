#import "src/tl/std/stdlib.tl"

#module main
// tZero := 0
// tOne := 1

EnumTag : {tZero, tOne, tTwo}
DupTag : {tZero, tFoo }

// #module main
main() {
  val := EnumTag.tZero
  val
}

// main() {
//   out : String := ()
//   d := c_strtod("1.234 foo bar", out.&)
//   c_printf("out = %p\n", out.*)
//   c_printf("out = '%s'\n", out)
//   c_printf("d = %f\n", d)
//   0
// }
