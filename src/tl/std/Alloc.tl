#module Alloc
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

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

context : Context

align     (n: CSize, alignment: CSize) -> CSize
align_max (n: CSize)                   -> CSize



// -- Implementation --



align(n: CSize, align: CSize) {
  c_assert(align)
  mask := align - 1
  (n + mask) & ~mask
}

align_max(n: CSize) {
  align(n, _tl_sizeof_(c_max_align_t)) // must use intrinsic due to C typedef
}

_malloc  (a: Ptr(Allocator), sz: CSize)              -> Ptr(any) { c_malloc(sz)      }
_calloc  (a: Ptr(Allocator), num: CSize,  sz: CSize) -> Ptr(any) { c_calloc(num, sz) }
_realloc (a: Ptr(Allocator), p: Ptr(any), sz: CSize) -> Ptr(any) { c_realloc(p, sz)  }
_free    (a: Ptr(Allocator), p: Ptr(any))            -> Void     { c_free(p)         }

_transient := Allocator(
  malloc  = _malloc,
  calloc  = _calloc,
  realloc = _realloc,
  free    = _free
)

module_init() {
  context = Context(
    transient = _transient.&,
    periodic  = _transient.&,
    managed   = _transient.&
  )
}
