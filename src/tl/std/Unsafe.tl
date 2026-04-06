#module Unsafe

#ifc
void*     tl_unsafe_pointer_add        (void const*ptr, size_t n)          { return ((char*)ptr) + n;                   }
void*     tl_unsafe_pointer_subtract   (void const*ptr, size_t n)          { return ((char*)ptr) - n;                   }
ptrdiff_t tl_unsafe_pointer_difference (void const*lhs, void const*rhs)    { return ((char*)lhs) - ((char*)rhs);        }
int       tl_unsafe_pointer_compare    (void const*lhs, void const*rhs)    { return lhs < rhs ? -1 : lhs > rhs ? 1 : 0; }

long long tl_unsafe_float_to_int       (double d)                { return (long long) d; }
double    tl_unsafe_int_to_float       (long long i)             { return (double) i;    }
#endc

c_tl_unsafe_pointer_add        (ptr: Ptr[Const[any]], n: CSize)             -> Ptr[any]
c_tl_unsafe_pointer_subtract   (ptr: Ptr[Const[any]], n: CSize)             -> Ptr[any]
c_tl_unsafe_pointer_difference (lhs: Ptr[Const[any]], rhs: Ptr[Const[any]]) -> CPtrDiff
c_tl_unsafe_pointer_compare    (lhs: Ptr[Const[any]], rhs: Ptr[Const[any]]) -> CInt

c_tl_unsafe_float_to_int (d: CDouble)   -> CLongLong
c_tl_unsafe_int_to_float (i: CLongLong) -> CDouble

c_tl_unsigned_to_signed(u: CUnsignedLongLong) -> CLongLong
c_tl_signed_to_unsigned(s: CLongLong)         -> CUnsignedLongLong


// Note that these cast away const.
pointer_add      (ptr: Ptr[Const[any]], n: CSize) -> Ptr[any] { c_tl_unsafe_pointer_add(ptr, n)      }
pointer_subtract (ptr: Ptr[Const[any]], n: CSize) -> Ptr[any] { c_tl_unsafe_pointer_subtract(ptr, n) }

pointer_compare (lhs: Ptr[Const[any]], rhs: Ptr[Const[any]]) -> CInt { c_tl_unsafe_pointer_compare(lhs, rhs) }

pointer_difference(lhs: Ptr[Const[any]], rhs: Ptr[Const[any]]) -> CPtrDiff {
    c_tl_unsafe_pointer_difference(lhs, rhs)
}

float_to_int(d) { c_tl_unsafe_float_to_int(d) }
int_to_float(i) { c_tl_unsafe_int_to_float(i) }
