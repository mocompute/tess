#module main
#import <cstdlib.tl>

main() {
    p: Ptr[Void] := c_malloc(64zu)
    c_free(p)
    0
}
