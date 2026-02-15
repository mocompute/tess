#module Foo
#import <stdlib.tl>

malloc(count: CSize) -> Ptr[any] {
    c_malloc(count)
}

Ctx: {
    malloc_fun: (n: CSize) -> Ptr[any],
}

allocator := Ctx(malloc_fun = malloc/1)


#module main
#import <stdlib.tl>

main() {
    p := Foo.allocator.malloc_fun(16)
    c_free(p)
    0
}
