#module main
#import <stdlib.tl>

// A function expecting a non-const nested pointer
mutate_indirect(pp: Ptr[Ptr(Int])) {
    pp.*.* = 10
    0
}

// Passing Ptr[Ptr(Const[Int]]) where Ptr[Ptr(Int]) is expected should fail
pass_nested_const(pp: Ptr[Ptr(Const[Int]])) {
    mutate_indirect(pp)    // error: cannot strip const
}

main() {
    p: Ptr[Ptr(Const[Int]]) := c_malloc(8)
    pass_nested_const(p)
}
