#module main
#import <stdio.tl>
#import <stdlib.tl>

read_through_const_ptr(p: Ptr(Const(Int))) {
    p.*
}

add_one_through_const(p: Ptr(Const(Int))) {
    p.* + 1
}

main() {
    // Basic: const pointer to Int
    p: Ptr(Int) := c_malloc(8)
    p.* = 42

    // Implicit coercion: Ptr(Int) -> Ptr(Const(Int))
    val := read_through_const_ptr(p)
    c_printf("val = %d\n", val)

    val2 := add_one_through_const(p)
    c_printf("val2 = %d\n", val2)

    c_free(p)
    0
}
