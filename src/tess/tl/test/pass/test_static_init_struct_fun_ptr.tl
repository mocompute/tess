#module Foo
#import <cstdlib.tl>

malloc(count: CSize) -> Ptr[any] {
    c_malloc(count)
}

Ctx: {
    malloc_fun: (n: CSize) -> Ptr[any],
}

allocator := Ctx(malloc_fun = malloc/1)


#module main
#import <cstdlib.tl>

main() {
    p := Foo.allocator.malloc_fun(16u)
    c_free(p)
    0
}
