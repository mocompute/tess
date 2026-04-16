// Documents that .cmp on numeric array elements works, so Array.cmp
// can be refactored later to delegate to element cmp uniformly.
#module main
#import <Array.tl>

cmp_first[T: Ord](a: Ptr[Array[T]], b: Ptr[Array[T]]) -> CInt {
    if a->size == 0 || b->size == 0 { return 0 }
    a->data.[0].cmp(b->data.[0])
}

main() -> CInt {
    a := Array.empty[Int]()
    b := Array.empty[Int]()
    defer a.free()
    defer b.free()
    a.push(10)
    b.push(20)
    if cmp_first(a.&, b.&) >= 0 { return 1 }
    0
}
