// Test: Allocator receiver methods (alloc, clear, realloc, dealloc, aligned_alloc, aligned_dealloc, resize).

#module main
#import <Alloc.tl>

main() -> CInt {
    a     := Alloc.context.default
    error := 0

    // alloc + dealloc
    p := a.alloc(64)
    if p == null { error += 1 }
    a.dealloc(p)

    // clear (zero-initialized)
    p2: Ptr[CSize] := a.clear(4, sizeof[CSize]())
    if p2 == null { error += 1 }
    if p2.* != 0  { error += 1 }
    a.dealloc(p2)

    // realloc
    p3 := a.alloc(32)
    p4 := a.realloc(p3, 128)
    if p4 == null { error += 1 }
    a.dealloc(p4)

    // aligned_alloc + aligned_dealloc
    p5 := a.aligned_alloc(64, 256)
    if p5 == null { error += 1 }
    if !Alloc.is_aligned(p5, 256) { error += 1 }
    a.aligned_dealloc(p5)

    // resize (managed allocator returns null — no in-place resize)
    p6 := a.alloc(64)
    p7 := a.resize(p6, 128)
    if p7 != null { error += 1 }
    a.dealloc(p6)

    error
}
