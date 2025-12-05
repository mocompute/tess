#module Foo
#import <stdlib.tl>

malloc(count: CSize) -> Ptr(Int) {
  c_malloc(count)
}

Ctx : {
  malloc_fun: (n: CSize) -> Ptr(Int),
}

allocator := Ctx(malloc_fun = malloc)


#module main
#import <stdlib.tl>

main() {
  p := Foo.allocator.malloc_fun(16)
  c_free(p)
  0
}
