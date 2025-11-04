#module Stdlib
#include <stdlib.h>

c_atof          (nptr: String) -> Float
c_atoi          (nptr: String) -> Int
c_atol          (nptr: String) -> Int
c_atoll         (nptr: String) -> Int
c_strtod        (nptr: String, endptr: Ptr(String)) -> Float

// float              strtof  (const char* restrict nptr, char** restrict endptr); // C99
// long double        strtold (const char* restrict nptr, char** restrict endptr); // C99
// long               strtol  (const char* restrict nptr, char** restrict endptr, int base);
// long long          strtoll (const char* restrict nptr, char** restrict endptr, int base); // C99
// unsigned long      strtoul (const char* restrict nptr, char** restrict endptr, int base);
// unsigned long long strtoull(const char* restrict nptr, char** restrict endptr, int base); // C99
// int rand(void);
// void srand(unsigned int seed);
// void* calloc(size_t nmemb, size_t size);
// void free(void* ptr);
// void* malloc(size_t size);
// void* realloc(void* ptr, size_t size);
// void abort(void);
// int atexit(void (*func)(void));
// void exit(int status);
// void _Exit(int status);
// char* getenv(const char* name);
// int system(const char* string);
// void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
//               int (*compar)(const void *, const void *));
// void qsort(void* base, size_t nmemb, size_t size,
//            int (*compar)(const void *, const void *));
// int         abs(      int j);
// long        abs(     long j);
// long long   abs(long long j);                                             // C++0X
// long       labs(     long j);
// long long llabs(long long j);                                             // C99
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


c_rand  ()              -> Int
c_srand (seed: Int)     -> Nil
c_aligned_alloc(alignment: Int, size: Int) -> Ptr(T)
c_calloc (num: Int, size_each: Int) -> Ptr(T)
c_free   (ptr: Ptr(T))  -> Nil
