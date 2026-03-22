#module main
#import <cstdio.tl>
#import <cstdlib.tl>

foo() { c_printf(c"goodbye.\n"), void }

main() {
    c_atexit(foo/0)
    0
}
