// [[alloc(expr)]] accepts Ptr[Const[Allocator]] as the explicit allocator
// shape. Whether the allocator's methods accept a const self is a separate
// concern caught at method dispatch.
#module main
#import <Alloc.tl>

Allocator = Alloc.Allocator

main() {
    a: Ptr[Const[Allocator]] := Alloc.context.default
    n := 10
    f := [[alloc(a), capture(n)]] (x) { x + n }
    if f(5) == 15 { 0 } else { 1 }
}
