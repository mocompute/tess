#module main
#import <stdio.tl>

id(x, y) { x + y }

foo() {
  res := id(123, abc)
  0
}

main() {
  res := id(123, abc)
  foo()
  // c_printf("alignof int: %zu\n", alignofXXX(Int))
  0
}
