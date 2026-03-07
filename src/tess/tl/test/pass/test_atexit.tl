#module main
#import <stdio.tl>
#import <stdlib.tl>

foo() { c_printf(c"goodbye.\n") }

main() {
    c_atexit(foo/0)
    0
}
