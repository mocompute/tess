#module stdlib
#include <stdlib.h>
#include <assert.h>

// assert.h
c_assert       (condition: Bool)                               -> Void

// stdlib.h
c_atof         (nptr: String)                                  -> Float
c_atoi         (nptr: String)                                  -> Int
c_atol         (nptr: String)                                  -> Int
c_atoll        (nptr: String)                                  -> Int
c_strtod       (nptr: String, endptr: Ptr(String))             -> Float
c_getenv       (name: String)                                  -> String
c_system       (str: String)                                   -> Int
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

c_strtof       (nptr: String, endptr: Ptr(String))             -> CFloat
c_strtold      (nptr: String, endptr: Ptr(String))             -> CLongDouble
c_strtol       (nptr: String, endptr: Ptr(String), base: CInt) -> CLong
c_strtoll      (nptr: String, endptr: Ptr(String), base: CInt) -> CLongLong
c_strtoul      (nptr: String, endptr: Ptr(String), base: CInt) -> CUnsignedLong
c_strtoull     (nptr: String, endptr: Ptr(String), base: CInt) -> CUnsignedLongLong
c_atexit       (f: () -> Void)                                 -> Void
c__Exit        (status: CInt)                                  -> Void

// void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
//               int (*compar)(const void *, const void *));
// void qsort(void* base, size_t nmemb, size_t size,
//            int (*compar)(const void *, const void *));
// div_t     div(      int numer,       int denom);
// ldiv_t    div(     long numer,      long denom);
// lldiv_t   div(long long numer, long long denom);                          // C++0X
// ldiv_t   ldiv(     long numer,      long denom);
// lldiv_t lldiv(long long numer, long long denom);                          // C99
// int mblen(const char* s, size_t n);
// int mbtowc(wchar_t* restrict pwc, const char* restrict s, size_t n);
// int wctomb(char* s, wchar_t wchar);
// size_t mbstowcs(wchar_t* restrict pwcs, const char* restrict s, size_t n);
// size_t wcstombs(char* restrict s, const wchar_t* restrict pwcs, size_t n);
// int at_quick_exit(void (*func)(void))                                     // C++11
// void quick_exit(int status);                                              // C++11
// void *aligned_alloc(size_t alignment, size_t size);                       // C11
