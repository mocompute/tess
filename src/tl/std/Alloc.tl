#module Alloc
#include <stddef.h>
#include <stdint.h>
#import <stdlib.tl>
#import <stdint.tl>
#import <string.tl>

#import <Unsafe.tl>

#ifc
// MSVC may not define max_align_t in stddef.h
#if defined(_MSC_VER)
typedef double max_align_t;
#endif

size_t tl_align_padding(void *p, size_t alignment) {
    uintptr_t addr = (uintptr_t)p;
    return (size_t)(((addr + alignment - 1) & ~(alignment - 1)) - addr);
}

int tl_is_aligned(void *p, size_t alignment) {
    return ((uintptr_t)p % alignment) == 0;
}
#endc

c_tl_align_padding (p: Ptr[any], alignment: CSize) -> CSize
c_tl_is_aligned    (p: Ptr[any], alignment: CSize) -> CInt

// -- Synopsis --

// Define TL_ALLOC_TRANSIENT_SIZE and TL_ALLOC_STRINGS_SIZE before importing (e.g. with -D in CFLAGS) to
// override default values.
initialTransientSize:      CSize := 0zu
initialStringLiteralsSize: CSize := 0zu

Allocator: {
    malloc:        (a: Ptr[Allocator], sz: CSize)                   -> Ptr[any],
    calloc:        (a: Ptr[Allocator], num: CSize, sz: CSize)        -> Ptr[any],
    realloc:       (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any],
    free:          (a: Ptr[Allocator], p: Ptr[any])                -> Void,
    aligned_alloc: (a: Ptr[Allocator], sz: CSize, alignment: CSize)  -> Ptr[any],
    aligned_free:  (a: Ptr[Allocator], p: Ptr[any])                -> Void,
    resize:        (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any],
}

Context: {
    transient:       Ptr[Allocator],
    managed:         Ptr[Allocator],
    string_literals: Ptr[Allocator],    // used by default by Str.from_cstr/1
    default:         Ptr[Allocator],
}

// Bump allocator bucket header. A bucket is a header followed by zero or more _BumpBlocks.
_BumpHeader: {
    next:     Ptr[_BumpHeader],
    capacity: CSize,
    size:     CSize,
}

// Bump allocator block allocated by a client. Client data follows the size field.
_BumpBlock: {
    size: CSize,
}

BumpAllocator: {
    allocator:      Allocator,
    parent:         Ptr[Allocator],
    head:           Ptr[_BumpHeader],
    peak_allocated: CSize,
}

BumpStats: {
    allocated:      CSize,
    capacity:       CSize,
    peak_allocated: CSize,
    bucket_count:   CSize,
}

BumpState: {
    bucket: Ptr[_BumpHeader],
    size:   CSize,
}

//

align         (n: CSize, alignment: CSize)    -> CSize
align_max     (n: CSize)                    -> CSize
align_padding (p: Ptr[any], alignment: CSize) -> CSize
is_aligned    (p: Ptr[any], alignment: CSize) -> Bool

transient_alloc         (sz: CSize)                   -> Ptr[any]
transient_clear         (num: CSize, sz: CSize)        -> Ptr[any]
transient_realloc       (p: Ptr[any], sz: CSize)      -> Ptr[any]
transient_free          (p: Ptr[any])                -> Void
transient_aligned_alloc (sz: CSize, alignment: CSize)  -> Ptr[any]
transient_aligned_free  (p: Ptr[any])                -> Void
transient_resize        (p: Ptr[any], sz: CSize)      -> Ptr[any]
transient_reset         ()                           -> Void
transient_save          ()                           -> BumpState
transient_restore       (state: BumpState)           -> Void

managed_alloc         (sz: CSize)                   -> Ptr[any]
managed_clear         (num: CSize, sz: CSize)        -> Ptr[any]
managed_realloc       (p: Ptr[any], sz: CSize)      -> Ptr[any]
managed_free          (p: Ptr[any])                -> Void
managed_aligned_alloc (sz: CSize, alignment: CSize)  -> Ptr[any]
managed_aligned_free  (p: Ptr[any])                -> Void
managed_resize        (p: Ptr[any], sz: CSize)      -> Ptr[any]

bump_reset     (a: Ptr[Allocator])                   -> Void
bump_save      (a: Ptr[Allocator])                   -> BumpState
bump_restore   (a: Ptr[Allocator], state: BumpState) -> Void
bump_get_stats (a: Ptr[Allocator])                   -> BumpStats

// -- Implementation --


align(n: CSize, align: CSize) {
    c_assert(align != 0zu)
    mask := align - 1zu
    (n + mask) & ~mask
}

align_max(n: CSize) {
    align(n, _tl_sizeof_(c_max_align_t)) // must use intrinsic due to C typedef
}

align_padding(p: Ptr[any], alignment: CSize) -> CSize {
    c_tl_align_padding(p, alignment)
}

is_aligned(p: Ptr[any], alignment: CSize) -> Bool {
    c_tl_is_aligned(p, alignment) != 0
}

// -- wrappers around stdlib allocator --
// Access them via managed_alloc, etc.

_malloc  (a: Ptr[Allocator], sz: CSize)              -> Ptr[any] { c_malloc(sz)      }
_calloc  (a: Ptr[Allocator], num: CSize, sz: CSize)   -> Ptr[any] { c_calloc(num, sz) }
_realloc (a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] { c_realloc(p, sz)  }
_free    (a: Ptr[Allocator], p: Ptr[any])           -> Void     { c_free(p)         }

_aligned_alloc (a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any] {
    c_aligned_alloc(alignment, sz)
}

_aligned_free (a: Ptr[Allocator], p: Ptr[any]) -> Void {
    c_aligned_free(p)
}

_resize (a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    null                                // stdlib has no in-place resize
}

// -- BumpAllocator --

_bump_malloc        (a: Ptr[Allocator], sz: CSize)                   -> Ptr[any]
_bump_calloc        (a: Ptr[Allocator], num: CSize, sz: CSize)        -> Ptr[any]
_bump_realloc       (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any]
_bump_free          (a: Ptr[Allocator], p: Ptr[any])                -> Void
_bump_aligned_alloc (a: Ptr[Allocator], sz: CSize, alignment: CSize)  -> Ptr[any]
_bump_resize        (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any]

bump_create(alloc: Ptr[Allocator], size: CSize) -> Ptr[Allocator] {
    out: Ptr[BumpAllocator] := alloc->malloc(alloc, sizeof[BumpAllocator]())
    out->parent         = alloc
    out->peak_allocated = 0zu
    size := next_power_of_two(size)
    size = if size == 0zu { 16zu } else { size }

    out->head                    = _BumpHeader_create(alloc, size)
    out->allocator.malloc        = _bump_malloc/2
    out->allocator.calloc        = _bump_calloc/3
    out->allocator.realloc       = _bump_realloc/3
    out->allocator.free          = _bump_free/2
    out->allocator.aligned_alloc = _bump_aligned_alloc/3
    out->allocator.aligned_free  = _bump_free/2
    out->allocator.resize        = _bump_resize/3
    out->allocator.&
}

bump_destroy(alloc: Ptr[Allocator]) {
    bump: Ptr[BumpAllocator] := alloc
    bump->parent->free(alloc, bump)
}

_bump_alloc_assume_capacity(bucket: Ptr[_BumpHeader], sz: CSize) -> Ptr[any] {
    p: Ptr[Byte] := bucket                  // cast
    p = Unsafe.pointer_add(p, bucket->size) // advance to free section

    block: Ptr[_BumpBlock] := p         // cast to block header and set size
    block->size = sz

    bucket->size += sz + sizeof[_BumpBlock]()  // advance bucket size past header and requested size
    c_assert(bucket->size <= bucket->capacity) // this function assumes capacity

    res: Ptr[Byte] := block                       // cast
    Unsafe.pointer_add(res, sizeof[_BumpBlock]()) // advance pointer past header and return it
}

_block_size(p: Ptr[any]) -> Ptr[CSize] {
    block: Ptr[_BumpBlock] := Unsafe.pointer_subtract(p, sizeof[CSize]())
    block->size.&
}

_is_last_block(bucket: Ptr[_BumpHeader], p: Ptr[any]) {
    if p < bucket { return false }
    sz         := _block_size(p).*
    p_end      := Unsafe.pointer_add(p, sz)
    bucket_end := Unsafe.pointer_add(bucket, bucket->size)

    p_end == bucket_end
}

_maybe_free_block(bucket: Ptr[_BumpHeader], p: Ptr[any]) {
    if _is_last_block(bucket, p) { bucket->size -= _block_size(p).* + sizeof[_BumpBlock]() }
    void
}

_bucket_has_capacity(bucket: Ptr[_BumpHeader], size: CSize) {
    bucket->capacity - bucket->size >= sizeof[_BumpBlock]() + size
}

_find_bucket(arena: Ptr[BumpAllocator], p: Ptr[any]) {
    bucket := arena->head
    c_assert(bucket != null)
    while bucket != null {
        if Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, sizeof[_BumpHeader]())) == 1 &&
           Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, bucket->size)) == -1
        {
            return bucket
        }
        bucket = bucket->next
    }

    null
}

_BumpHeader_create(alloc, capacity) {
    c_assert(capacity > sizeof[_BumpHeader]())
    out: Ptr[_BumpHeader] := alloc->malloc(alloc, capacity)
    out->next     = null
    out->capacity = capacity
    out->size     = sizeof[_BumpHeader]()
    out
}

// Intentionally duplicates bucket-walk logic from _bump_aligned_alloc to avoid padding overhead on the common path.
_bump_malloc(a: Ptr[Allocator], sz: CSize) -> Ptr[any] {
    arena: Ptr[BumpAllocator] := a

    bucket                 := arena->head
    last: Ptr[_BumpHeader] := null
    last_capacity: CSize   := 0zu

    sz = align_max(sz)
    if 0zu == sz { return null }

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
    needed       := sz + sizeof[_BumpHeader]()
    new_capacity := last_capacity * 2zu
    if new_capacity < needed { new_capacity = next_power_of_two(needed) }
    if new_capacity == 0zu { return null } // overflow

    last->next = _BumpHeader_create(arena->parent, new_capacity)
    if last->next == null { return null }
    bucket = last->next
    _bump_alloc_assume_capacity(bucket, sz)
}

// In-place resize logic intentionally duplicated from _bump_resize to avoid a double _find_bucket walk.
_bump_realloc(a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    if p == null { return _bump_malloc(a, sz) }

    sz = align_max(sz)
    arena: Ptr[BumpAllocator] := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null {
        c_assert(false)
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

_bump_calloc(a: Ptr[Allocator], num: CSize, sz: CSize) -> Ptr[any] {
    sz = align_max(sz)
    out := _bump_malloc(a, num * sz)
    if out != null { c_memset(out, 0, num * sz) }
    out
}

_bump_free(a: Ptr[Allocator], p: Ptr[any]) -> Void {
    if p == null { return void }

    arena: Ptr[BumpAllocator] := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null {
        c_assert(false)                     // attempt to free unknown pointer
        return void
    }

    if _is_last_block(bucket, p) {
        bucket->size -= _block_size(p).* + sizeof[_BumpBlock]()
    }
}

// Intentionally duplicates bucket-walk logic from _bump_malloc to interleave alignment padding with bucket
// selection.
_bump_aligned_alloc(a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any] {
    if alignment <= _tl_sizeof_(c_max_align_t) {
        return _bump_malloc(a, sz)
    }

    arena: Ptr[BumpAllocator] := a

    sz = align_max(sz)
    if 0zu == sz { return null }

    // Upper bound on space needed: sz + alignment covers worst-case padding
    total_needed := sz + alignment

    bucket                 := arena->head
    last: Ptr[_BumpHeader] := null
    last_capacity: CSize   := 0zu

    c_assert(bucket != null)
    while bucket != null {
        if _bucket_has_capacity(bucket, total_needed) {
            // Compute padding so user pointer lands on an aligned address
            user_ptr := Unsafe.pointer_add(bucket, bucket->size + sizeof[_BumpBlock]())
            padding  := c_tl_align_padding(user_ptr, alignment)
            bucket->size += padding
            return _bump_alloc_assume_capacity(bucket, sz)
        }

        last_capacity = bucket->capacity
        last          = bucket
        bucket        = bucket->next
    }

    // Need a new bucket
    needed       := total_needed + sizeof[_BumpHeader]() + sizeof[_BumpBlock]()
    new_capacity := last_capacity * 2zu
    if new_capacity < needed { new_capacity = next_power_of_two(needed) }
    if new_capacity == 0zu { return null }

    last->next = _BumpHeader_create(arena->parent, new_capacity)
    if last->next == null { return null }
    bucket = last->next

    user_ptr := Unsafe.pointer_add(bucket, bucket->size + sizeof[_BumpBlock]())
    padding  := c_tl_align_padding(user_ptr, alignment)
    bucket->size += padding
    _bump_alloc_assume_capacity(bucket, sz)
}

// In-place resize logic intentionally duplicated from _bump_realloc to avoid a double _find_bucket walk.
_bump_resize(a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    if p == null { return null }

    sz = align_max(sz)
    arena: Ptr[BumpAllocator] := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null { return null }

    cur_size_p := _block_size(p)
    cur_size   := cur_size_p.*
    if sz == cur_size { return p }

    if _is_last_block(bucket, p) {
        if sz < cur_size {
            cur_size_p.* = sz
            bucket->size -= cur_size - sz
            return p
        }

        if _bucket_has_capacity(bucket, sz - cur_size) {
            cur_size_p.* = sz
            bucket->size += sz - cur_size
            return p
        }
    }

    null
}


// Returns: input if already a power of two, or else the next higher
// power of two.
next_power_of_two(n: CSize) {
    if n > (c_SIZE_MAX / 2zu) + 1zu {return 0zu} // overflow

    if (n == 0zu) { return 1zu }

    // set all bits to the right of the highest set bit by masking.
    n = n - 1zu
    n = n | (n >> 1zu)
    n = n | (n >> 2zu)
    n = n | (n >> 4zu)
    n = n | (n >> 8zu)
    n = n | (n >> 16zu)
    n = n | (n >> 32zu)
    n + 1zu
}

// -- helpers --

_bump_current_allocated(a: Ptr[Allocator]) -> CSize {
    arena: Ptr[BumpAllocator] := a
    total: CSize               := 0zu
    bucket                    := arena->head
    while bucket != null {
        total += bucket->size - sizeof[_BumpHeader]()
        bucket = bucket->next
    }
    total
}

bump_reset(a: Ptr[Allocator]) -> Void {
    arena: Ptr[BumpAllocator] := a

    // Update peak before resetting
    current := _bump_current_allocated(a)
    if current > arena->peak_allocated {
        arena->peak_allocated = current
    }

    bucket := arena->head
    while bucket != null {
        bucket->size = sizeof[_BumpHeader]()
        bucket       = bucket->next
    }
}

bump_get_stats(a: Ptr[Allocator]) -> BumpStats {
    arena: Ptr[BumpAllocator] := a

    allocated:    CSize := 0zu
    capacity:     CSize := 0zu
    bucket_count: CSize := 0zu

    bucket := arena->head
    while bucket != null {
        allocated += bucket->size - sizeof[_BumpHeader]()
        capacity += bucket->capacity - sizeof[_BumpHeader]()
        bucket_count += 1zu
        bucket = bucket->next
    }

    peak := arena->peak_allocated
    if allocated > peak { peak = allocated }

    BumpStats(
              allocated      = allocated,
              capacity       = capacity,
              peak_allocated = peak,
              bucket_count   = bucket_count
              )
}

bump_save(a: Ptr[Allocator]) -> BumpState {
    arena: Ptr[BumpAllocator] := a
    bucket                    := arena->head
    while bucket->next != null {
        bucket = bucket->next
    }
    BumpState(bucket = bucket, size = bucket->size)
}

bump_restore(a: Ptr[Allocator], state: BumpState) -> Void {
    arena: Ptr[BumpAllocator] := a

    // Update peak before restoring
    current := _bump_current_allocated(a)
    if current > arena->peak_allocated {
        arena->peak_allocated = current
    }

    // Reset saved bucket's size
    state.bucket->size = state.size

    // Free all buckets after the saved one
    next := state.bucket->next
    while next != null {
        following := next->next
        arena->parent->free(arena->parent, next)
        next = following
    }
    state.bucket->next = null
}

// -- transient --

transient_alloc         (sz)            { context.transient->malloc(context.transient, sz)                   }
transient_clear         (num, sz)       { context.transient->calloc(context.transient, num, sz)              }
transient_realloc       (p, sz)         { context.transient->realloc(context.transient, p, sz)               }
transient_free          (p)             { context.transient->free(context.transient, p)                      }
transient_aligned_alloc (sz, alignment) { context.transient->aligned_alloc(context.transient, sz, alignment) }
transient_aligned_free  (p)             { context.transient->aligned_free(context.transient, p)              }
transient_resize        (p, sz)         { context.transient->resize(context.transient, p, sz)                }
transient_reset         ()              { bump_reset(context.transient)                                      }
transient_save          ()              { bump_save(context.transient)                                       }
transient_restore       (state)         { bump_restore(context.transient, state)                             }

// -- managed --

managed_alloc         (sz)            { context.managed->malloc(context.managed, sz)                   }
managed_clear         (num, sz)       { context.managed->calloc(context.managed, num, sz)              }
managed_realloc       (p, sz)         { context.managed->realloc(context.managed, p, sz)               }
managed_free          (p)             { context.managed->free(context.managed, p)                      }
managed_aligned_alloc (sz, alignment) { context.managed->aligned_alloc(context.managed, sz, alignment) }
managed_aligned_free  (p)             { context.managed->aligned_free(context.managed, p)              }
managed_resize        (p, sz)         { context.managed->resize(context.managed, p, sz)                }

// -- initialisation --

// Define these with -D in CFLAGS during compilation to override the defaults.
#ifc
#ifndef TL_ALLOC_TRANSIENT_SIZE
#define TL_ALLOC_TRANSIENT_SIZE 4096
#endif
#ifndef TL_ALLOC_STRINGS_SIZE
#define TL_ALLOC_STRINGS_SIZE 1024
#endif
#endc

c_TL_ALLOC_TRANSIENT_SIZE: CSize
c_TL_ALLOC_STRINGS_SIZE:   CSize

_managed := Allocator(
                      malloc        = _malloc/2,
                      calloc        = _calloc/3,
                      realloc       = _realloc/3,
                      free          = _free/2,
                      aligned_alloc = _aligned_alloc/3,
                      aligned_free  = _aligned_free/2,
                      resize        = _resize/3
                      )

// Allocation Context. Public so that user code can (carefully) install its own allocators dynamically.
context := Context(
                   transient       = void, // initialized in __init()
                   string_literals = void, // initialized in __init()
                   managed         = _managed.&,
                   default         = _managed.&
                   )


// Module init
__init() {
    initialTransientSize      = c_TL_ALLOC_TRANSIENT_SIZE
    initialStringLiteralsSize = c_TL_ALLOC_STRINGS_SIZE
    context.transient         = bump_create(context.managed, initialTransientSize)
    context.string_literals   = bump_create(context.managed, initialStringLiteralsSize)
}
