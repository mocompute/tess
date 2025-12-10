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
  trans: Ptr(Allocator),
  periodic:  Ptr(Allocator),
  managed:   Ptr(Allocator),
}



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
  trans = _transient.&,
  periodic  = _periodic.&,
  managed   = _managed.&
)


// module_init() {
//   context = Context(
//     transient = _transient.&,
//     periodic  = _transient.&,
//     managed   = _transient.&
//   )
// }


// -- transient --

transient(sz: CSize) { context.trans->malloc(sz) }

// clear_transient   (num: CSize,  sz: CSize) -> Ptr(any)
// realloc_transient (p: Ptr(any), sz: CSize) -> Ptr(any)
// free_transient    (p: Ptr(any))            -> Void
