#module Unsafe

#ifc
void*     tl_unsafe_pointer_add        (void *ptr, size_t n)     { return ((char*)ptr) + n;                   }
void*     tl_unsafe_pointer_subtract   (void *ptr, size_t n)     { return ((char*)ptr) - n;                   }
ptrdiff_t tl_unsafe_pointer_difference (void *lhs, void *rhs)    { return ((char*)lhs) - ((char*)rhs);        }
int       tl_unsafe_pointer_compare    (void *lhs, void *rhs)    { return lhs < rhs ? -1 : lhs > rhs ? 1 : 0; }

long long tl_unsafe_float_to_int       (double d)                { return (long long) d; }
double    tl_unsafe_int_to_float       (long long i)             { return (double) i;    }

long long tl_unsafe_unsigned_to_signed         (unsigned long long l)     { return (long long) l;          }
unsigned long long tl_unsafe_signed_to_unsigned(long long l)              { return (unsigned long long) l; }
#endc

c_tl_unsafe_pointer_add        (ptr: Ptr[any], n: CSize)      -> Ptr[any]
c_tl_unsafe_pointer_subtract   (ptr: Ptr[any], n: CSize)      -> Ptr[any]
c_tl_unsafe_pointer_difference (lhs: Ptr[any], rhs: Ptr[any]) -> CPtrDiff
c_tl_unsafe_pointer_compare    (lhs: Ptr[any], rhs: Ptr[any]) -> CInt

c_tl_unsafe_float_to_int (d: CDouble)   -> CLongLong
c_tl_unsafe_int_to_float (i: CLongLong) -> CDouble

c_tl_unsigned_to_signed(u: CUnsignedLongLong) -> CLongLong
c_tl_signed_to_unsigned(s: CLongLong)         -> CUnsignedLongLong


pointer_add      (ptr: Ptr[any], n: UInt)      -> Ptr[any] { c_tl_unsafe_pointer_add(ptr, n)       }
pointer_subtract (ptr: Ptr[any], n: UInt)      -> Ptr[any] { c_tl_unsafe_pointer_subtract(ptr, n)  }
pointer_compare  (lhs: Ptr[any], rhs: Ptr[any]) -> CInt     { c_tl_unsafe_pointer_compare(lhs, rhs) }

pointer_difference(lhs: Ptr[any], rhs: Ptr[any]) -> CPtrDiff {
    c_tl_unsafe_pointer_difference(lhs, rhs)
}

float_to_int(d) { c_tl_unsafe_float_to_int(d) }
int_to_float(i) { c_tl_unsafe_int_to_float(i) }

unsigned_to_signed(u) { c_tl_unsafe_unsigned_to_signed(u) }
signed_to_unsigned(s) { c_tl_unsafe_signed_to_unsigned(s) }
