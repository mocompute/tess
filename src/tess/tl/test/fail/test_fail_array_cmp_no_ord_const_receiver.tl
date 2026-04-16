// Array.cmp called with a Const receiver on a type with no Ord implementation.
// Const is stripped from both sides so T is correctly bound to Thing.
// The trait check then reports that Thing does not satisfy Ord.
#module main
#import <Array.tl>

Thing: { val: Int }

main() {
    b: Const := Array.empty[Thing]()
    a        := Array.empty[Thing]()
    b.cmp(a)
    0
}
