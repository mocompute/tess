#module Alloc
#include <stddef.h>
#include <stdint.h>
#import <cstdlib.tl>
#import <cstdint.tl>
#import <cstring.tl>

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

// -- Types --

// Define TL_ALLOC_TRANSIENT_SIZE before importing (e.g. with -D in CFLAGS) to
// override default values.
initialTransientSize: CSize := 0

Allocator: {
    malloc:        (a: Ptr[Allocator], sz: CSize)                   -> Ptr[any],
    calloc:        (a: Ptr[Allocator], num: CSize, sz: CSize)       -> Ptr[any],
    realloc:       (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any],
    free:          (a: Ptr[Allocator], p: Ptr[any])                 -> Void,
    aligned_alloc: (a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any],
    aligned_free:  (a: Ptr[Allocator], p: Ptr[any])                 -> Void,
    resize:        (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any],
}

Context: {
    transient: Ptr[Allocator],
    managed:   Ptr[Allocator],
    default:   Ptr[Allocator],
}

// ============================================================================
// Bump Allocator submodule
// ============================================================================

#module Alloc.BumpAllocator
Allocator = Alloc.Allocator


// Bump allocator bucket header. A bucket is a header followed by zero or more _Blocks.
#module Alloc.BumpAllocator._Header
Allocator = Alloc.Allocator

_Header: {
    next:     Ptr[_Header],
    capacity: CSize,
    size:     CSize,
    _pad:     CSize,          // align to sizeof(max_align_t)
}

create(alloc: Ptr[Allocator], capacity: CSize) -> Ptr[_Header]

#module Alloc.BumpAllocator

// Bump allocator block allocated by a client. Client data follows the size field.
_Block: {
    size: CSize,
    _pad: CSize,              // align to sizeof(max_align_t)
}

BumpAllocator: {
    allocator:      Allocator,
    parent:         Ptr[Allocator],
    head:           Ptr[_Header],
    peak_allocated: CSize,
}

Stats: {
    allocated:      CSize,
    capacity:       CSize,
    peak_allocated: CSize,
    bucket_count:   CSize,
}

State: {
    bucket: Ptr[_Header],
    size:   CSize,
}

// -- Synopsis --

create    (alloc: Ptr[Allocator], size: CSize) -> Ptr[Allocator]
destroy   (alloc: Ptr[Allocator])              -> Void
reset     (a: Ptr[Allocator])                  -> Void
save      (a: Ptr[Allocator])                  -> State
restore   (a: Ptr[Allocator], state: State)    -> Void
get_stats (a: Ptr[Allocator])                  -> Stats

// -- Implementation --

#module Alloc.BumpAllocator._Header

create(alloc, capacity) {
    c_assert(capacity > sizeof[_Header]())
    out: Ptr[_Header] := alloc.alloc(capacity)
    out->next     = null
    out->capacity = capacity
    out->size     = sizeof[_Header]()
    out
}

#module Alloc.BumpAllocator

_malloc        (a: Ptr[Allocator], sz: CSize)                   -> Ptr[any]
_calloc        (a: Ptr[Allocator], num: CSize, sz: CSize)       -> Ptr[any]
_realloc       (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any]
_free          (a: Ptr[Allocator], p: Ptr[any])                 -> Void
_aligned_alloc (a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any]
_resize        (a: Ptr[Allocator], p: Ptr[any], sz: CSize)      -> Ptr[any]


create(alloc: Ptr[Allocator], size: CSize) -> Ptr[Allocator] {
    out: Ptr[BumpAllocator] := alloc.alloc(sizeof[BumpAllocator]())
    out->parent         = alloc
    out->peak_allocated = 0
    size := Alloc.next_power_of_two(size)
    size = if size == 0 { 16 } else { size }

    out->head                    = _Header.create(alloc, size)
    out->allocator.malloc        = _malloc/2
    out->allocator.calloc        = _calloc/3
    out->allocator.realloc       = _realloc/3
    out->allocator.free          = _free/2
    out->allocator.aligned_alloc = _aligned_alloc/3
    out->allocator.aligned_free  = _free/2
    out->allocator.resize        = _resize/3
    out->allocator.&
}

destroy(alloc: Ptr[Allocator]) {
    bump: Ptr[BumpAllocator] := alloc
    bump->parent.dealloc(bump)
}

_alloc_assume_capacity(bucket: Ptr[_Header], sz: CSize) -> Ptr[any] {
    p: Ptr[Byte] := bucket                  // cast
    p = Unsafe.pointer_add(p, bucket->size) // advance to free section

    block: Ptr[_Block] := p             // cast to block header and set size
    block->size = sz

    bucket->size += sz + sizeof[_Block]()      // advance bucket size past header and requested size
    c_assert(bucket->size <= bucket->capacity) // this function assumes capacity

    res: Ptr[Byte] := block                   // cast
    Unsafe.pointer_add(res, sizeof[_Block]()) // advance pointer past header and return it
}

_block_size(p: Ptr[any]) -> Ptr[CSize] {
    block: Ptr[_Block] := Unsafe.pointer_subtract(p, sizeof[_Block]())
    block->size.&
}

_is_last_block(bucket: Ptr[_Header], p: Ptr[any]) {
    if p < bucket { return false }
    sz         := _block_size(p).*
    p_end      := Unsafe.pointer_add(p, sz)
    bucket_end := Unsafe.pointer_add(bucket, bucket->size)

    p_end == bucket_end
}

_maybe_free_block(bucket: Ptr[_Header], p: Ptr[any]) {
    if _is_last_block(bucket, p) { bucket->size -= _block_size(p).* + sizeof[_Block]() }
    void
}

_bucket_has_capacity(bucket: Ptr[_Header], size: CSize) {
    bucket->capacity - bucket->size >= sizeof[_Block]() + size
}

_find_bucket(arena: Ptr[BumpAllocator], p: Ptr[any]) {
    bucket := arena->head
    c_assert(bucket != null)
    while bucket != null {
        if Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, sizeof[_Header]())) == 1 &&
           Unsafe.pointer_compare(p, Unsafe.pointer_add(bucket, bucket->size)) == -1
        {
            return bucket
        }
        bucket = bucket->next
    }

    null
}

// Intentionally duplicates bucket-walk logic from _aligned_alloc to avoid padding overhead on the common path.
_malloc(a: Ptr[Allocator], sz: CSize) -> Ptr[any] {
    arena: Ptr[BumpAllocator] := a

    bucket                      := arena->head
    last:          Ptr[_Header] := null
    last_capacity: CSize        := 0

    sz = Alloc.align_max(sz)
    if 0 == sz { return null }

    c_assert(bucket != null)
    while bucket != null {
        if _bucket_has_capacity(bucket, sz) {
            return _alloc_assume_capacity(bucket, sz)
        }

        last_capacity = bucket->capacity
        last          = bucket
        bucket        = bucket->next
    }

    // otherwise, need to allocate a new bucket
    needed       := sz + sizeof[_Header]()
    new_capacity := last_capacity * 2
    if new_capacity < needed { new_capacity = Alloc.next_power_of_two(needed) }
    if new_capacity == 0 { return null } // overflow

    last->next = _Header.create(arena->parent, new_capacity)
    if last->next == null { return null }
    bucket = last->next
    _alloc_assume_capacity(bucket, sz)
}

// In-place resize logic intentionally duplicated from _resize to avoid a double _find_bucket walk.
_realloc(a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    if p == null { return _malloc(a, sz) }

    sz = Alloc.align_max(sz)
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
    new_block := _malloc(a, sz)
    c_assert(sz >= cur_size)
    c_memcpy(new_block, p, cur_size)
    _maybe_free_block(bucket, p)
    new_block
}

_calloc(a: Ptr[Allocator], num: CSize, sz: CSize) -> Ptr[any] {
    sz = Alloc.align_max(sz)
    out := _malloc(a, num * sz)
    if out != null { c_memset(out, 0, num * sz) }
    out
}

_free(a: Ptr[Allocator], p: Ptr[any]) -> Void {
    if p == null { return void }

    arena: Ptr[BumpAllocator] := a
    bucket                    := _find_bucket(arena, p)
    if bucket == null {
        c_assert(false)                 // attempt to free unknown pointer
        return void
    }

    if _is_last_block(bucket, p) {
        bucket->size -= _block_size(p).* + sizeof[_Block]()
    }
}

// Intentionally duplicates bucket-walk logic from _malloc to interleave alignment padding with bucket
// selection.
_aligned_alloc(a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any] {
    if alignment <= _tl_sizeof_(c_max_align_t) {
        return _malloc(a, sz)
    }

    arena: Ptr[BumpAllocator] := a

    sz = Alloc.align_max(sz)
    if 0 == sz { return null }

    // Upper bound on space needed: sz + alignment covers worst-case padding
    total_needed := sz + alignment

    bucket                      := arena->head
    last:          Ptr[_Header] := null
    last_capacity: CSize        := 0

    c_assert(bucket != null)
    while bucket != null {
        if _bucket_has_capacity(bucket, total_needed) {
            // Compute padding so user pointer lands on an aligned address
            user_ptr := Unsafe.pointer_add(bucket, bucket->size + sizeof[_Block]())
            padding  := c_tl_align_padding(user_ptr, alignment)
            bucket->size += padding
            return _alloc_assume_capacity(bucket, sz)
        }

        last_capacity = bucket->capacity
        last          = bucket
        bucket        = bucket->next
    }

    // Need a new bucket
    needed       := total_needed + sizeof[_Header]() + sizeof[_Block]()
    new_capacity := last_capacity * 2
    if new_capacity < needed { new_capacity = Alloc.next_power_of_two(needed) }
    if new_capacity == 0 { return null }

    last->next = _Header.create(arena->parent, new_capacity)
    if last->next == null { return null }
    bucket = last->next

    user_ptr := Unsafe.pointer_add(bucket, bucket->size + sizeof[_Block]())
    padding  := c_tl_align_padding(user_ptr, alignment)
    bucket->size += padding
    _alloc_assume_capacity(bucket, sz)
}

// In-place resize logic intentionally duplicated from _realloc to avoid a double _find_bucket walk.
_resize(a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    if p == null { return null }

    sz = Alloc.align_max(sz)
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

_current_allocated(a: Ptr[Allocator]) -> CSize {
    arena: Ptr[BumpAllocator] := a
    total: CSize              := 0
    bucket                    := arena->head
    while bucket != null {
        total += bucket->size - sizeof[_Header]()
        bucket = bucket->next
    }
    total
}

reset(a: Ptr[Allocator]) -> Void {
    arena: Ptr[BumpAllocator] := a

    // Update peak before resetting
    current := _current_allocated(a)
    if current > arena->peak_allocated {
        arena->peak_allocated = current
    }

    bucket := arena->head
    while bucket != null {
        bucket->size = sizeof[_Header]()
        bucket       = bucket->next
    }
}

get_stats(a: Ptr[Allocator]) -> Stats {
    arena: Ptr[BumpAllocator] := a

    allocated:    CSize := 0
    capacity:     CSize := 0
    bucket_count: CSize := 0

    bucket := arena->head
    while bucket != null {
        allocated += bucket->size - sizeof[_Header]()
        capacity += bucket->capacity - sizeof[_Header]()
        bucket_count += 1
        bucket = bucket->next
    }

    peak := arena->peak_allocated
    if allocated > peak { peak = allocated }

    Stats(
          allocated      = allocated,
          capacity       = capacity,
          peak_allocated = peak,
          bucket_count   = bucket_count
          )
}

save(a: Ptr[Allocator]) -> State {
    arena: Ptr[BumpAllocator] := a
    bucket                    := arena->head
    while bucket->next != null {
        bucket = bucket->next
    }
    State(bucket = bucket, size = bucket->size)
}

restore(a: Ptr[Allocator], state: State) -> Void {
    arena: Ptr[BumpAllocator] := a

    // Update peak before restoring
    current := _current_allocated(a)
    if current > arena->peak_allocated {
        arena->peak_allocated = current
    }

    // Reset saved bucket's size
    state.bucket->size = state.size

    // Free all buckets after the saved one
    next := state.bucket->next
    while next != null {
        following := next->next
        arena->parent.dealloc(next)
        next = following
    }
    state.bucket->next = null
}

// ============================================================================
// Alloc module (continued)
// ============================================================================

#module Alloc

// -- Synopsis --

align         (n: CSize, alignment: CSize)    -> CSize
align_max     (n: CSize)                      -> CSize
align_padding (p: Ptr[any], alignment: CSize) -> CSize
is_aligned    (p: Ptr[any], alignment: CSize) -> Bool

(self: Ptr[Allocator]): {
    alloc          (sz: CSize)                   -> Ptr[any]
    clear          (num: CSize, sz: CSize)       -> Ptr[any]
    realloc        (p: Ptr[any], sz: CSize)      -> Ptr[any]
    dealloc        (p: Ptr[any])                 -> Void
    aligned_alloc  (sz: CSize, alignment: CSize) -> Ptr[any]
    aligned_dealloc(p: Ptr[any])                 -> Void
    resize         (p: Ptr[any], sz: CSize)      -> Ptr[any]
}

// Returns: input if already a power of two, or else the next higher
// power of two.
next_power_of_two(n: CSize) -> CSize

// -- Implementation --

align(n: CSize, align: CSize) {
    c_assert(align != 0)
    mask := align - 1
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

_malloc  (a: Ptr[Allocator], sz: CSize)              -> Ptr[any] { c_malloc(sz)      }
_calloc  (a: Ptr[Allocator], num: CSize, sz: CSize)  -> Ptr[any] { c_calloc(num, sz) }
_realloc (a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] { c_realloc(p, sz)  }
_free    (a: Ptr[Allocator], p: Ptr[any])            -> Void     { c_free(p)         }

_aligned_alloc (a: Ptr[Allocator], sz: CSize, alignment: CSize) -> Ptr[any] {
    // C11 requires size to be a multiple of alignment; round up to satisfy this.
    c_aligned_alloc(alignment, align(sz, alignment))
}

_aligned_free (a: Ptr[Allocator], p: Ptr[any]) -> Void {
    c_aligned_free(p)
}

_resize (a: Ptr[Allocator], p: Ptr[any], sz: CSize) -> Ptr[any] {
    null                                // stdlib has no in-place resize
}

(self: Ptr[Allocator]): {
    alloc          (sz)            { self->malloc(self, sz)                   }
    clear          (num, sz)       { self->calloc(self, num, sz)              }
    realloc        (p, sz)         { self->realloc(self, p, sz)               }
    dealloc        (p)             { self->free(self, p)                      }
    aligned_alloc  (sz, alignment) { self->aligned_alloc(self, sz, alignment) }
    aligned_dealloc(p)             { self->aligned_free(self, p)              }
    resize         (p, sz)         { self->resize(self, p, sz)                }
}

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

// -- initialisation --

// Define these with -D in CFLAGS during compilation to override the defaults.
#ifc
#ifndef TL_ALLOC_TRANSIENT_SIZE
#define TL_ALLOC_TRANSIENT_SIZE 4096
#endif
#endc

c_TL_ALLOC_TRANSIENT_SIZE: CSize

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
                   transient = void,    // initialized in __init()
                   managed   = _managed.&,
                   default   = _managed.&
                   )


// Module init
__init() {
    initialTransientSize = c_TL_ALLOC_TRANSIENT_SIZE
    context.transient    = BumpAllocator.create(context.managed, initialTransientSize)
}
