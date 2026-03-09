#module main
#import <Alloc.tl>

// [[alloc(arena)]] where arena is Ptr[Allocator] must pass type checking.
// In known_failures because Phase 4 codegen doesn't handle alloc closures yet.

main() {
    bump := Alloc.bump_create(Alloc.context.transient, 1024u)
    n := 42
    f := [[alloc(bump), capture(n)]] (x) { x + n }
    error := 0
    if f(10) != 52 { error += 1 }
    error
}
