#module main
#import <stdlib.tl>

mutate_index(p: Ptr[Const[Int]]) {
    p.[0] = 10                          // error: const violation
    0
}

main() {
    p: Ptr[Int] := c_malloc(8)
    p.* = 42
    mutate_index(p)
}
