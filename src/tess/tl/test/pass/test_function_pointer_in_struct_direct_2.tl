#module Foo
#import <cstdlib.tl>

// Return type annotation is required because this function will not be specialized, because it is only
// every referenced through a function pointer.
malloc(count: CSize) -> Ptr[Int] {
    c_malloc(count)
}

Ctx[t1]: {
    malloc_fun: t1,
}

init() {
    Ctx(malloc_fun = malloc/1)
}


#module main
#import <cstdlib.tl>

main() {
    ctx := Foo.init()
    p   := ctx.malloc_fun(16)
    c_free(p)
    0
}
