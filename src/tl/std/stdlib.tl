#module stdlib
#include <stdlib.h>
#include <assert.h>


// assert.h
c_assert       (condition: Bool)                               -> Void

// stdlib.h
c_atof         (nptr: CString)                                  -> Float
c_atoi         (nptr: CString)                                  -> Int
c_atol         (nptr: CString)                                  -> Int
c_atoll        (nptr: CString)                                  -> Int
c_strtod       (nptr: CString, endptr: Ptr(CString))             -> Float
c_getenv       (name: CString)                                  -> CString
c_system       (str: CString)                                   -> Int
c_rand         ()                                              -> CInt
c_srand        (seed: CUnsignedInt)                            -> Void
c_aligned_alloc(alignment: CSize, size: CSize)                 -> Ptr(T)
c_calloc       (num: CSize, size_each: CSize)                  -> Ptr(T)
c_free         (ptr: Ptr(T))                                   -> Void
c_malloc       (size: CSize)                                   -> Ptr(T)
c_realloc      (ptr: Ptr(T), size: CSize)                      -> Ptr(T)
c_exit         (status: CInt)                                  -> Void
c_abort        ()                                              -> Void

c_abs          (j: CInt)                                       -> CInt
c_labs         (j: CLong)                                      -> CLong
c_llabs        (j: CLongLong)                                  -> CLongLong

c_strtof       (nptr: CString, endptr: Ptr(CString))             -> CFloat
c_strtold      (nptr: CString, endptr: Ptr(CString))             -> CLongDouble
c_strtol       (nptr: CString, endptr: Ptr(CString), base: CInt) -> CLong
c_strtoll      (nptr: CString, endptr: Ptr(CString), base: CInt) -> CLongLong
c_strtoul      (nptr: CString, endptr: Ptr(CString), base: CInt) -> CUnsignedLong
c_strtoull     (nptr: CString, endptr: Ptr(CString), base: CInt) -> CUnsignedLongLong
c_atexit       (f: () -> Void)                                 -> Void
c__Exit        (status: CInt)                                  -> Void
c_bsearch      (key: Ptr(Void),
                base: Ptr(Void),
                nmemb: CSize,
                size: CSize,
                compar: (a: Ptr(Void), b: Ptr(Void)) -> CInt)  -> CInt
c_qsort        (base: Ptr(Void),
                nmemb: CSize,
                size: CSize,
                compar: (a: Ptr(Void), b: Ptr(Void)) -> CInt)  -> Void


c_div_t   : { quot: CInt,      rem: CInt      }
c_ldiv_t  : { quot: CLong,     rem: CLong     }
c_lldiv_t : { quot: CLongLong, rem: CLongLong }

c_div          (numer: CInt,      denom: CInt)                 -> c_div_t
c_ldiv         (numer: CLong,     denom: CLong)                -> c_ldiv_t
c_lldiv        (numer: CLongLong, denom: CLongLong)            -> c_lldiv_t


// int mblen(const char* s, size_t n);
// int mbtowc(wchar_t* restrict pwc, const char* restrict s, size_t n);
// int wctomb(char* s, wchar_t wchar);
// size_t mbstowcs(wchar_t* restrict pwcs, const char* restrict s, size_t n);
// size_t wcstombs(char* restrict s, const wchar_t* restrict pwcs, size_t n);
// int at_quick_exit(void (*func)(void))                                     // C++11
// void quick_exit(int status);                                              // C++11
// void *aligned_alloc(size_t alignment, size_t size);                       // C11
