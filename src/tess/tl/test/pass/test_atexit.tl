#module main
#import <cstdio.tl>
#import <cstdlib.tl>

foo() { c_printf("goodbye.\n") }

main() {
    c_atexit(foo/0)
    0
}
