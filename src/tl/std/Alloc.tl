#module Alloc
#include <assert.h>

align(n: CSize, align: CSize) {
  c_assert(align)
  mask := align - 1
  (n + mask) & ~mask
}
