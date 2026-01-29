#module main
#import <stdlib.tl>

main() {
    p := c_malloc(16)
    p[0] = 123
    pp := p.&

    val := pp.*.*
    c_free(p)
    val - 123
}
