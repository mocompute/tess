#module main
#import <stdlib.tl>

main() {
    p: Ptr[Int] := c_malloc(16zu)
    p.[0] = 123
    pp := p.&

    val := pp.*.*
    c_free(p)
    val - 123
}
