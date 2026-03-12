// Bug: sizeof[T] unifies T across separate call sites when used with
// nested generic structs.
//
// When a function calls sizeof[Inner[K,V]] and a separate function
// (or the same function) calls sizeof[Outer[K,V]] where Outer contains
// Ptr[Inner[K,V]], the specializer unifies Inner with Outer — a type error.
#module main
#import <Alloc.tl>

Inner[K, V]: { key: K, value: V }

Outer[K, V]: {
    items: Ptr[Inner[K, V]],
    size:  CSize,
}

_alloc_inner[K, V](alloc: Ptr[Alloc.Allocator], n: CSize) -> Ptr[Inner[K, V]] {
    p: Ptr[Inner[K, V]] := alloc->calloc(alloc, n, sizeof[Inner[K, V]]())
    p
}

make[K, V]() -> Ptr[Outer[K, V]] {
    alloc               := Alloc.context.default
    m: Ptr[Outer[K, V]] := alloc->malloc(alloc, sizeof[Outer[K, V]]())
    m->items = _alloc_inner[K, V](alloc, 16zu)
    m->size  = 0zu
    m
}

main() {
    m := make[Int, Int]()
    if m->size != 0zu { return 1 }
    0
}
