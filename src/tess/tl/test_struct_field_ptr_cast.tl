#module main
#import <stdlib.tl>

Foo(T): {v: Ptr(T)}

main() {
    foo := Foo(v: Ptr(Int) = c_malloc(sizeof(Int) * 4))
    foo.v[0] = 1
    c_free(foo.v)
    0
}
