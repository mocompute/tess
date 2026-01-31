#module main
#import <stdlib.tl>

// Compound assignment through const pointer
mutate_compound(p: Ptr(Const(Int))) {
    p.* += 1    // error: const violation via compound assignment
    0
}

main() {
    p: Ptr(Int) := c_malloc(8)
    p.* = 42
    mutate_compound(p)
}
