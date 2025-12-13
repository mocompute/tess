#module Unsafe

#ifc
void*     tl_unsafe_pointer_add        (void *ptr, ptrdfiff_t n) { return ((char*)ptr) + n; }
ptrdiff_t tl_unsafe_pointer_difference (void *lhs, void *rhs)    { return ((char*)lhs) - ((char*)rhs); }
#endc

c_tl_unsafe_pointer_add        (ptr: Ptr(any), n: CPtrDiff)   -> Ptr(any)
c_tl_unsafe_pointer_difference (lhs: Ptr(any), rhs: Ptr(any)) -> CPtrDiff

pointer_add      (ptr: Ptr(any), n: CPtrdiff) -> Ptr(any) { c_tl_unsafe_pointer_add(ptr, n)  }
pointer_subtract (ptr: Ptr(any), n: CPtrdiff) -> Ptr(any) { c_tl_unsafe_pointer_add(ptr, -n) }

pointer_difference(lhs: Ptr(any), rhs: Ptr(any)) -> CPtrDiff {
  c_tl_unsafe_pointer_difference(lhs, rhs)
}
