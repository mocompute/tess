// Array.cmp called when the receiver is a Const-qualified binding.
// b.& is Ptr[Const[Array[Int]]], so the first callsite argument has Const on the
// concrete side. collect_quant_bindings strips Const from both sides to correctly
// bind T = Int and satisfy the Ord bound.
#module main
#import <Array.tl>

main() -> CInt {
    b: Const := Array.empty[Int]()
    a        := Array.empty[Int]()
    defer a.free()
    if b.cmp(a) != 0 { return 1 }
    0
}
