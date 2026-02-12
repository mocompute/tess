#module main
#import <Alloc.tl>

#import <stdlib.tl>

_BumpHeader: {
    next:     Ptr[_BumpHeader],
    capacity: CSize,
    size:     CSize,
}

align(n: CSize, align: CSize) {
    c_assert(align != 0)
    mask := align - 1
    (n + mask) & ~mask
}

_type_width_aligned[T]() { align(sizeof[T](), alignof[T]()) }

main() {

    width := _type_width_aligned[CInt]()

    bh := _BumpHeader(next = null, capacity = 0, size = 0)


    width
}
