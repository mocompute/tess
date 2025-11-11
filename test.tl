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


_grow(self: Ptr(T), at_least: Int) {
  if self->capacity >= at_least { return }
  count := self->capacity
  if count < at_least { count = count * 2 } // FIXME: silent parse failure
  while count < at_least { count = count * 2 } // FIXME: parses as type constructor
  reserve(self, count)
}
