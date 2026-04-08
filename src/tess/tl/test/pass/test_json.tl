#module main

#import <Alloc.tl>
#import <Json.tl>

// Use a module alias, not a type alias
#alias Arena Alloc.BumpAllocator

main() {

    arena := Arena.create(Alloc.context.managed, 1024)

    // unfortunate cannot use UFCS due to type erasure: arena is a Ptr[Allocator]
    defer Arena.destroy(arena)

    json := Json.Context.create(arena)
    defer json.destroy()

    0
}
