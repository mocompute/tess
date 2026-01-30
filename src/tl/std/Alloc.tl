#module Alloc
#include <stddef.h>
#import <stdlib.tl>
#import <stdint.tl>
#import <string.tl>

#import <Unsafe.tl>

#ifc
// MSVC may not define max_align_t in stddef.h
#if defined(_MSC_VER)
typedef double max_align_t;
#endif
#endc

// -- Synopsis --

// Define TL_ALLOC_TRANSIENT_SIZE and TL_ALLOC_PERIODIC_SIZE before importing (e.g. with -D in CFLAGS) to
// override default values.
initialTransientSize: CSize := 0
initialPeriodicSize:  CSize := 0

Allocator: {
    malloc:  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any),
    calloc:  (a: Ptr(Allocator), num: CSize, sz: CSize)  -> Ptr(any),
    realloc: (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any),
    free:    (a: Ptr(Allocator), p: Ptr(any))            -> Void,
}

Context: {
    transient: Ptr(Allocator),
    periodic:  Ptr(Allocator),
    managed:   Ptr(Allocator),
}

// Bump allocator bucket header. A bucket is a header followed by zero or more _BumpBlocks.
_BumpHeader: {
    next:     Ptr(_BumpHeader),
    capacity: CSize,
    size:     CSize,
}

// Bump allocator block allocated by a client. Client data follows the size field.
_BumpBlock: {
    size: CSize,
}

BumpAllocator: {
    allocator: Allocator,
    parent:    Ptr(Allocator),
    head:      Ptr(_BumpHeader),
}

//

align     (n: CSize, alignment: CSize) -> CSize
align_max (n: CSize)                   -> CSize

transient_alloc   (sz: CSize)              -> Ptr(any)
transient_clear   (num: CSize, sz: CSize)  -> Ptr(any)
transient_realloc (p: Ptr(any), sz: CSize) -> Ptr(any)
transient_free    (p: Ptr(any))            -> Void

periodic_alloc   (sz: CSize)              -> Ptr(any)
periodic_clear   (num: CSize, sz: CSize)  -> Ptr(any)
periodic_realloc (p: Ptr(any), sz: CSize) -> Ptr(any)
periodic_free    (p: Ptr(any))            -> Void

managed_alloc   (sz: CSize)              -> Ptr(any)
managed_clear   (num: CSize, sz: CSize)  -> Ptr(any)
managed_realloc (p: Ptr(any), sz: CSize) -> Ptr(any)
managed_free    (p: Ptr(any))            -> Void

// -- Implementation --


align(n: CSize, align: CSize) {
    c_assert(align != 0)
    mask := align - 1
    (n + mask) & ~mask
}

align_max(n: CSize) {
    align(n, _tl_sizeof_(c_max_align_t)) // must use intrinsic due to C typedef
}

// -- wrappers around stdlib allocator --
// Access them via managed_alloc, etc.

_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any) { c_malloc(sz)      }
_calloc  (a: Ptr(Allocator), num: CSize, sz: CSize)  -> Ptr(any) { c_calloc(num, sz) }
_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any) { c_realloc(p, sz)  }
_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void     { c_free(p)         }

// -- BumpAllocator --

_bump_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any)
_bump_calloc  (a: Ptr(Allocator), num: CSize, sz: CSize)  -> Ptr(any)
_bump_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any)
_bump_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void

bump_create(alloc: Ptr(Allocator), size: CSize) -> Ptr(Allocator) {
    out: Ptr(BumpAllocator) := alloc->malloc(alloc, sizeof(BumpAllocator))
    out->parent = alloc
    size := next_power_of_two(size)
    size = if size == 0 { 16 } else { size }

    out->head              = _BumpHeader_create(alloc, size)
    out->allocator.malloc  = _bump_malloc/2
    out->allocator.calloc  = _bump_calloc/3
    out->allocator.realloc = _bump_realloc/3
    out->allocator.free    = _bump_free/2
    out->allocator.&
}

bump_destroy(alloc: Ptr(Allocator)) {
    bump: Ptr(BumpAllocator) := alloc
    bump->parent->free(alloc, bump)
}

_bump_alloc_assume_capacity(bucket: Ptr(_BumpHeader), sz: CSize) -> Ptr(any) {
    p: Ptr(Byte) := bucket                  // cast
    p = Unsafe.pointer_add(p, bucket->size) // advance to free section

    block: Ptr(_BumpBlock) := p         // cast to block header and set size
    block->size = sz

    bucket->size += sz + sizeof(_BumpBlock)    // advance bucket size past header and requested size
    c_assert(bucket->size <= bucket->capacity) // this function assumes capacity

    res: Ptr(Byte) := block                     // cast
    Unsafe.pointer_add(res, sizeof(_BumpBlock)) // advance pointer past header and return it
}

_block_size(p: Ptr(any)) -> Ptr(CSize) {
    block: Ptr(_BumpBlock) := Unsafe.pointer_subtract(p, sizeof(CSize))
    block->size.&
}

_is_last_block(bucket: Ptr(_BumpHeader), p: Ptr(any)) {
    if p < bucket { return 0 }
    sz         := _block_size(p).*
    p_end      := Unsafe.pointer_add(p, sz)
    bucket_end := Unsafe.pointer_add(bucket, bucket->size)

    p_end == bucket_end
}

_maybe_free_block(bucket: Ptr(_BumpHeader), p: Ptr(any)) {
    if _is_last_block(bucket, p) { bucket->size -= _block_size(p).* + sizeof(_BumpBlock) }
    void
}

_bucket_has_capacity(bucket: Ptr(_BumpHeader), size: CSize) {
    bucket->capacity - bucket->size >= sizeof(_BumpBlock) + size
}

_find_bucket(arena: Ptr(BumpAllocator), p: Ptr(any)) {
    bucket := arena->head
    c_assert(bucket != null)
    while bucket != null {
        if Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, sizeof(_BumpHeader))) == 1 &&
           Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, bucket->size)) == -1
        {
            return bucket
        }
        bucket = bucket->next
    }

    null
}

_BumpHeader_create(alloc, capacity) {
    c_assert(capacity > sizeof(_BumpHeader))
    out: Ptr(_BumpHeader) := alloc->malloc(alloc, capacity)
    out->next     = null
    out->capacity = capacity
    out->size     = sizeof(_BumpHeader)
    out
}

_bump_malloc(a: Ptr(Allocator), sz: CSize) -> Ptr(any) {
    arena: Ptr(BumpAllocator) := a

    bucket                 := arena->head
    last: Ptr(_BumpHeader) := null
    last_capacity          := 0

    sz = align_max(sz)
    if 0 == sz { return null }

    c_assert(bucket != null)
    while bucket != null {
        if _bucket_has_capacity(bucket, sz) {
            return _bump_alloc_assume_capacity(bucket, sz)
        }

        last_capacity = bucket->capacity
        last          = bucket
        bucket        = bucket->next
    }

    // otherwise, need to allocate a new bucket
    needed       := sz + sizeof(_BumpHeader)
    new_capacity := last_capacity * 2
    if new_capacity < needed { new_capacity = next_power_of_two(needed) }
    if new_capacity == 0 { return null } // overflow

    last->next = _BumpHeader_create(arena->parent, new_capacity)
    if last->next == null { return null }
    bucket = last->next
    _bump_alloc_assume_capacity(bucket, sz)
}

_bump_realloc(a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any) {
    if p == null { return _bump_malloc(a, sz) }

    sz = align_max(sz)
    arena: Ptr(BumpAllocator) := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null {
        c_assert(0)
        return null
    }

    cur_size_p := _block_size(p)
    cur_size   := cur_size_p.*
    if sz == cur_size { return p }

    if _is_last_block(bucket, p) {
        // shrink block in place
        if sz < cur_size {
            cur_size_p.* = sz
            bucket->size -= cur_size - sz
            return p
        }

        // grow block if there is room in its bucket
        if _bucket_has_capacity(bucket, sz - cur_size) {
            cur_size_p.* = sz
            bucket->size += sz - cur_size
            return p
        }
    }

    if sz < cur_size { return p }       // cannot shrink non-last block

    // allocate new block, copy data, release old block if possible
    new_block := _bump_malloc(a, sz)
    c_assert(sz >= cur_size)
    c_memcpy(new_block, p, cur_size)
    _maybe_free_block(bucket, p)
    new_block
}

_bump_calloc(a: Ptr(Allocator), num: CSize, sz: CSize) -> Ptr(any) {
    sz = align_max(sz)
    out := _bump_malloc(a, num * sz)
    if out != null { c_memset(out, 0xCD, num * sz) } // TODO: uses 0xCD for debugging, not 0x00
    out
}

_bump_free(a: Ptr(Allocator), p: Ptr(any)) -> Void {
    if p == null { return void }

    arena: Ptr(BumpAllocator) := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null {
        c_assert(0)                     // attempt to free unknown pointer
        return void
    }

    if _is_last_block(bucket, p) {
        bucket->size -= _block_size(p).* + sizeof(_BumpBlock)
    }
}


// Returns: input if already a power of two, or else the next higher
// power of two.
next_power_of_two(n: CSize) {
    if n > (c_SIZE_MAX / 2) + 1 {return 0} // overflow

    if (n == 0) { return 1 }

    // set all bits to the right of the highest set bit by masking.
    n = n - 1
    n = n | (n >> 1)
    n = n | (n >> 2)
    n = n | (n >> 4)
    n = n | (n >> 8)
    n = n | (n >> 16)
    n = n | (n >> 32)
    n + 1
}

// -- helpers --


// -- transient --

transient_alloc   (sz)      { context.transient->malloc(context.transient, sz)      }
transient_clear   (num, sz) { context.transient->calloc(context.transient, num, sz) }
transient_realloc (p, sz)   { context.transient->realloc(context.transient, p, sz)  }
transient_free    (p)       { context.transient->free(context.transient, p)         }

// -- periodic --

periodic_alloc   (sz)      { context.periodic->malloc(context.periodic, sz)      }
periodic_clear   (num, sz) { context.periodic->calloc(context.periodic, num, sz) }
periodic_realloc (p, sz)   { context.periodic->realloc(context.periodic, p, sz)  }
periodic_free    (p)       { context.periodic->free(context.periodic, p)         }

// -- managed --

managed_alloc   (sz)      { context.managed->malloc(context.managed, sz)      }
managed_clear   (num, sz) { context.managed->calloc(context.managed, num, sz) }
managed_realloc (p, sz)   { context.managed->realloc(context.managed, p, sz)  }
managed_free    (p)       { context.managed->free(context.managed, p)         }

// -- initialisation --

// Define these with -D in CFLAGS during compilation to override the defaults.
#ifc
#ifndef TL_ALLOC_TRANSIENT_SIZE
#define TL_ALLOC_TRANSIENT_SIZE 4096
#endif

#ifndef TL_ALLOC_PERIODIC_SIZE
#define TL_ALLOC_PERIODIC_SIZE  4096
#endif
#endc

c_TL_ALLOC_TRANSIENT_SIZE: CSize
c_TL_ALLOC_PERIODIC_SIZE:  CSize

_managed := Allocator(
                      malloc  = _malloc/2,
                      calloc  = _calloc/3,
                      realloc = _realloc/3,
                      free    = _free/2
                      )

// Allocation Context. Public so that user code can (carefully) install its own allocators dynamically.
context := Context(
                   transient = void,    // initialized in __init()
                   periodic  = void,    // initialized in __init()
                   managed   = _managed.&
                   )


// Module init
__init() {
    initialTransientSize = c_TL_ALLOC_TRANSIENT_SIZE
    initialPeriodicSize  = c_TL_ALLOC_PERIODIC_SIZE

    context.transient = bump_create(context.managed, initialTransientSize)
    context.periodic  = bump_create(context.managed, initialPeriodicSize)
}
