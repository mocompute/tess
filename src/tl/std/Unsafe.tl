#module Unsafe

#ifc
void*     tl_unsafe_pointer_add        (void *ptr, ptrdiff_t n)  { return ((char*)ptr) + n; }
ptrdiff_t tl_unsafe_pointer_difference (void *lhs, void *rhs)    { return ((char*)lhs) - ((char*)rhs); }
int       tl_unsafe_pointer_compare    (void *lhs, void *rhs)    { return lhs < rhs ? -1 : lhs > rhs ? 1 : 0; }
#endc

c_tl_unsafe_pointer_add        (ptr: Ptr(any), n: CPtrDiff)   -> Ptr(any)
c_tl_unsafe_pointer_difference (lhs: Ptr(any), rhs: Ptr(any)) -> CPtrDiff
c_tl_unsafe_pointer_compare    (lhs: Ptr(any), rhs: Ptr(any)) -> CInt

pointer_add      (ptr: Ptr(any), n: CPtrDiff) -> Ptr(any) { c_tl_unsafe_pointer_add(ptr, n)  }
pointer_subtract (ptr: Ptr(any), n: CPtrDiff) -> Ptr(any) { c_tl_unsafe_pointer_add(ptr, -n) }
pointer_compare  (lhs: Ptr(any), rhs: Ptr(any)) -> CInt   { c_tl_unsafe_pointer_compare(lhs, rhs) }

pointer_difference(lhs: Ptr(any), rhs: Ptr(any)) -> CPtrDiff {
  c_tl_unsafe_pointer_difference(lhs, rhs)
}
