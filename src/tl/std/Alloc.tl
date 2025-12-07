#module Alloc
#include <assert.h>
#include <stddef.h>

// -- Synopsis --

Allocator : {
  malloc : (a: Ptr(Allocator), sz: CSize) -> Ptr(any)
}

// Context : {

// }


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
