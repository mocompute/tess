#module main
#import <stdlib.tl>

main() {
    p: Ptr[Int] := c_malloc(8zu * sizeof[Int]())
    p.[1 + 2] = 0                       // Note: regression test simple binary op
    res := p.[3]
    c_free(p)
    result: CInt := res
    result
}
