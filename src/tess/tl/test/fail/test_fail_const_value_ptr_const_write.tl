#module main
#import <cstdlib.tl>

main() {
    raw: Ptr[Int]               := c_malloc(8zu)
    cpc: Const[Ptr[Const[Int]]] := raw
    cpc.* = 42
    c_free(raw)
    0
}
