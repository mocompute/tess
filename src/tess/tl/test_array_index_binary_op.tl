#module main
#import <stdlib.tl>

main() {
    p: Ptr[Int] := c_malloc(8 * sizeof[Int]())
    p.(1 + 2) = 0                       // Note: regression test simple binary op
    res := p.(3)
    c_free(p)
    res
}
