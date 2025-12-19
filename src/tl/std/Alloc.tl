#module Alloc
#import <stdlib.tl>

#import <Unsafe.tl>

// -- Synopsis --

Allocator : {
  malloc:  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any),
  calloc:  (a: Ptr(Allocator), num: CSize,  sz: CSize) -> Ptr(any),
  realloc: (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any),
  free:    (a: Ptr(Allocator), p: Ptr(any))            -> Void,
}

Context : {
  transient: Ptr(Allocator),
  periodic:  Ptr(Allocator),
  managed:   Ptr(Allocator),
}

// Bump allocator bucket header. A bucket is a header followed by zero or more _BumpBlocks.
_BumpHeader : {
  next:     Ptr(_BumpHeader),
  capacity: CSize,
  size:     CSize,
}

// Bump allocator block allocated by a client. Client data follows the size field.
_BumpBlock : {
  size:     CSize,
}

BumpAllocator : {
  allocator: Allocator,
  parent:    Ptr(Allocator),
  head:      Ptr(_BumpHeader),
}

//

align     (n: CSize, alignment: CSize) -> CSize
align_max (n: CSize)                   -> CSize

transient         (sz: CSize)              -> Ptr(any)
clear_transient   (num: CSize,  sz: CSize) -> Ptr(any)
realloc_transient (p: Ptr(any), sz: CSize) -> Ptr(any)
free_transient    (p: Ptr(any))            -> Void

periodic          (sz: CSize)              -> Ptr(any)
clear_periodic    (num: CSize,  sz: CSize) -> Ptr(any)
realloc_periodic  (p: Ptr(any), sz: CSize) -> Ptr(any)
free_periodic     (p: Ptr(any))            -> Void

managed           (sz: CSize)              -> Ptr(any)
clear_managed     (num: CSize,  sz: CSize) -> Ptr(any)
realloc_managed   (p: Ptr(any), sz: CSize) -> Ptr(any)
free_managed      (p: Ptr(any))            -> Void

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

_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any) { c_malloc(sz)      }
_calloc  (a: Ptr(Allocator), num: CSize,  sz: CSize) -> Ptr(any) { c_calloc(num, sz) }
_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any) { c_realloc(p, sz)  }
_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void     { c_free(p)         }

// FIXME: use arenas
_transient := Allocator(
  malloc  = _malloc,
  calloc  = _calloc,
  realloc = _realloc,
  free    = _free
)

_periodic := Allocator(
  malloc  = _malloc,
  calloc  = _calloc,
  realloc = _realloc,
  free    = _free
)

_managed := Allocator(
  malloc  = _malloc,
  calloc  = _calloc,
  realloc = _realloc,
  free    = _free
)

context := Context(
  transient = _transient.&,
  periodic  = _periodic.&,
  managed   = _managed.&
)


// -- transient --

// FIXME: these all use stdlib allocator for now, until we have a bump allocator

transient         (sz)      { context.transient->malloc(context.transient, sz)      }
clear_transient   (num, sz) { context.transient->calloc(context.transient, num, sz) }
realloc_transient (p, sz)   { context.transient->realloc(context.transient, p, sz)  }
free_transient    (p)       { context.transient->free(context.transient, p)         }

// -- periodic --

periodic          (sz)      { context.periodic->malloc(context.periodic, sz)        }
clear_periodic    (num, sz) { context.periodic->calloc(context.periodic, num, sz)   }
realloc_periodic  (p, sz)   { context.periodic->realloc(context.periodic, p, sz)    }
free_periodic     (p)       { context.periodic->free(context.periodic, p)           }

// -- managed --

managed           (sz)      { context.managed->malloc(context.managed, sz)          }
clear_managed     (num, sz) { context.managed->calloc(context.managed, num, sz)     }
realloc_managed   (p, sz)   { context.managed->realloc(context.managed, p, sz)      }
free_managed      (p)       { context.managed->free(context.managed, p)             }


// -- BumpAllocator --

_bump_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any)
_bump_calloc  (a: Ptr(Allocator), num: CSize,  sz: CSize) -> Ptr(any)
_bump_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any)
_bump_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void

// FIXME: implement these
_bump_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any) { c_malloc(sz)      }
_bump_calloc  (a: Ptr(Allocator), num: CSize,  sz: CSize) -> Ptr(any) { c_calloc(num, sz) }
_bump_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any) { c_realloc(p, sz)  }
_bump_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void     { c_free(p)         }


bump_create(alloc: Ptr(Allocator), size: CSize) {
  out : Ptr(BumpAllocator) := alloc->malloc(alloc, sizeof(BumpAllocator))
  out->parent = alloc
  size := next_power_of_two(size)
  size = if size == 0 { 16 } else { size }
  out->allocator.malloc  = _bump_malloc
  out->allocator.calloc  = _bump_calloc
  out->allocator.realloc = _bump_realloc
  out->allocator.free    = _bump_free
  out->allocator.&
}

bump_destroy(alloc: Ptr(Allocator)) {
  bump : Ptr(BumpAllocator) := alloc
  bump->parent->free(alloc, bump)
  void
}

_bump_alloc_assume_capacity(bucket: Ptr(_BumpHeader), sz: CSize) -> Ptr(any) {
  p: Ptr(Byte) := bucket                      // cast
  p += bucket->size                           // advance to free section

  block: Ptr(_BumpBlock) := p                 // cast to block header and set size
  block->size = sz

  bucket->size += sz + sizeof(_BumpBlock)     // advance bucket size past header and requested size
  c_assert(bucket->size <= bucket->capacity)  // this function assumes capacity

  res: Ptr(Byte) := block                     // cast
  Unsafe.pointer_add(res, sizeof(_BumpBlock)) // advance pointer past header and return it
}

_block_size(p: Ptr(any)) -> Ptr(CSize) {
  block: Ptr(_BumpBlock) := Unsafe.pointer_subtract(p, sizeof(CSize))
  block->size.&
}

_is_last_block(bucket: Ptr(_BumpHeader), p: Ptr(any)) {
  if p < bucket { return 0 }
  block: Ptr(_BumpBlock) := Unsafe.pointer_subtract(p, sizeof(CSize))
  sz := block->size
  p_end := Unsafe.pointer_add(p, sz)
  block_end := Unsafe.pointer_add(block, block->size)

  block_end == p_end
}


// Returns: input if already a power of two, or else the next higher
// power of two.
next_power_of_two(n: CSize) {

    if n > (c_SIZE_MAX / 2) + 1 {return 0} // overflow

    if (n == 0) {return 1}

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
