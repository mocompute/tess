#module main
#import <cstdlib.tl>

Foo[T]: {data: Ptr[T]}

main() {
    foo := Foo(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu))
    foo.data.[0] = 1
    c_free(foo.data)
    0
}
