#module stdlib
#import <cerrno.tl>
#include <stdlib.h>
#include <assert.h>

#ifc
// MSVC does not provide C11 aligned_alloc; use _aligned_malloc/_aligned_free instead.
#if defined(_MSC_VER)
#include <malloc.h>
static void* aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
static void aligned_free(void *p) {
    _aligned_free(p);
}
#else
static void aligned_free(void *p) {
    free(p);
}
#endif
#endc

// assert.h
c_assert (condition: Bool) -> Void

// stdlib.h
c_atof         (nptr: Ptr[Const[CChar]])       -> Float
c_atoi         (nptr: Ptr[Const[CChar]])       -> Int
c_atol         (nptr: Ptr[Const[CChar]])       -> Int
c_atoll        (nptr: Ptr[Const[CChar]])       -> Int
c_getenv       (name: Ptr[Const[CChar]])       -> CString
c_system       (str: Ptr[Const[CChar]])        -> Int
c_rand         ()                              -> CInt
c_srand        (seed: CUnsignedInt)            -> Void
c_aligned_alloc(alignment: CSize, size: CSize) -> Ptr[any]
c_aligned_free (ptr: Ptr[any])                 -> Void
c_calloc       (num: CSize, size_each: CSize)  -> Ptr[any]
c_free[T]      (ptr: Ptr[T])                   -> Void
c_malloc       (size: CSize)                   -> Ptr[any]
c_realloc[T]   (ptr: Ptr[T], size: CSize)      -> Ptr[any]
c_exit         (status: CInt)                  -> Void
c_abort        ()                              -> Void

c_abs   (j: CInt)      -> CInt
c_labs  (j: CLong)     -> CLong
c_llabs (j: CLongLong) -> CLongLong

c_strtof   (nptr: Ptr[Const[CChar]], endptr: Ptr[CString]) -> CFloat
c_strtod   (nptr: Ptr[Const[CChar]], endptr: Ptr[CString]) -> CDouble
c_strtold  (nptr: Ptr[Const[CChar]], endptr: Ptr[CString]) -> CLongDouble
c_strtol   (nptr: Ptr[Const[CChar]], endptr: Ptr[CString], base: CInt) -> CLong
c_strtoll  (nptr: Ptr[Const[CChar]], endptr: Ptr[CString], base: CInt) -> CLongLong
c_strtoul  (nptr: Ptr[Const[CChar]], endptr: Ptr[CString], base: CInt) -> CUnsignedLong
c_strtoull (nptr: Ptr[Const[CChar]], endptr: Ptr[CString], base: CInt) -> CUnsignedLongLong
c_atexit   (f: () -> Void) -> Void
c__Exit    (status: CInt) -> Void
c_bsearch  (key: Ptr[Const[Void]],
           base:   Ptr[Const[Void]],
           nmemb:  CSize,
           size:   CSize,
           compar: (a: Ptr[Const[Void]], b: Ptr[Const[Void]]) -> CInt) -> Ptr[Void]
c_qsort (base: Ptr[Void],
         nmemb:  CSize,
         size:   CSize,
         compar: (a: Ptr[Const[Void]], b: Ptr[Const[Void]]) -> CInt) -> Void


c_div_t:   { quot: CInt, rem: CInt           }
c_ldiv_t:  { quot: CLong, rem: CLong         }
c_lldiv_t: { quot: CLongLong, rem: CLongLong }

c_div   (numer: CInt, denom: CInt)           -> c_div_t
c_ldiv  (numer: CLong, denom: CLong)         -> c_ldiv_t
c_lldiv (numer: CLongLong, denom: CLongLong) -> c_lldiv_t


// int mblen(const char* s, size_t n);
// int mbtowc(wchar_t* restrict pwc, const char* restrict s, size_t n);
// int wctomb(char* s, wchar_t wchar);
// size_t mbstowcs(wchar_t* restrict pwcs, const char* restrict s, size_t n);
// size_t wcstombs(char* restrict s, const wchar_t* restrict pwcs, size_t n);
// int at_quick_exit(void (*func)(void))                                     // C++11
// void quick_exit(int status);                                              // C++11
// void *aligned_alloc(size_t alignment, size_t size);                       // C11
