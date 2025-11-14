#module Alloc
#include <assert.h>

align(n: CSize, align: CSize) { // FIXME: parser is mangling align to Alloc.align
  c_assert(align)
  mask := align - 1
  (n + mask) & ~mask
}
