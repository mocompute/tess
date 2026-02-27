// -- begin std --

// Thread-local storage compatibility
#ifdef _MSC_VER
#define TL_THREAD_LOCAL __declspec(thread)
#else
#define TL_THREAD_LOCAL _Thread_local
#endif

// DLL export attribute for shared library symbols
#ifdef _MSC_VER
#define TL_EXPORT __declspec(dllexport)
#else
#define TL_EXPORT __attribute__((visibility("default")))
#endif

// Integer narrowing bounds check (controlled by TL_BOUNDS_CHECK define)
#ifdef TL_BOUNDS_CHECK
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define tl_narrowing_assert(val, min, max, from_type, to_type, file, line) \
    do { \
        long long tl_na_v_ = (long long)(val); \
        if (tl_na_v_ < (min) || tl_na_v_ > (max)) { \
            fprintf(stderr, "%s:%d: narrowing conversion overflow: " \
                from_type " value %lld does not fit in " to_type \
                " (range %lld..%lld)\n", \
                (file), (line), \
                tl_na_v_, (long long)(min), (long long)(max)); \
            abort(); \
        } \
    } while(0)
#define tl_unsigned_narrowing_assert(val, max, from_type, to_type, file, line) \
    do { \
        unsigned long long tl_una_v_ = (unsigned long long)(val); \
        if (tl_una_v_ > (max)) { \
            fprintf(stderr, "%s:%d: narrowing conversion overflow: " \
                from_type " value %llu does not fit in " to_type \
                " (max %llu)\n", \
                (file), (line), \
                tl_una_v_, (unsigned long long)(max)); \
            abort(); \
        } \
    } while(0)
/* unsigned source -> signed target: check val fits in positive range of target */
#define tl_unsigned_to_signed_assert(val, max, from_type, to_type, file, line) \
    do { \
        unsigned long long tl_us_v_ = (unsigned long long)(val); \
        if (tl_us_v_ > (unsigned long long)(max)) { \
            fprintf(stderr, "%s:%d: narrowing conversion overflow: " \
                from_type " value %llu does not fit in " to_type \
                " (max %lld)\n", \
                (file), (line), \
                tl_us_v_, (long long)(max)); \
            abort(); \
        } \
    } while(0)
/* signed source -> unsigned target: check val is non-negative and fits */
#define tl_signed_to_unsigned_assert(val, max, from_type, to_type, file, line) \
    do { \
        long long tl_su_v_ = (long long)(val); \
        if (tl_su_v_ < 0 || (unsigned long long)tl_su_v_ > (max)) { \
            fprintf(stderr, "%s:%d: narrowing conversion overflow: " \
                from_type " value %lld does not fit in " to_type \
                " (range 0..%llu)\n", \
                (file), (line), \
                tl_su_v_, (unsigned long long)(max)); \
            abort(); \
        } \
    } while(0)
#else
#define tl_narrowing_assert(val, min, max, from_type, to_type, file, line) ((void)0)
#define tl_unsigned_narrowing_assert(val, max, from_type, to_type, file, line) ((void)0)
#define tl_unsigned_to_signed_assert(val, max, from_type, to_type, file, line) ((void)0)
#define tl_signed_to_unsigned_assert(val, max, from_type, to_type, file, line) ((void)0)
#endif

// -- end std --
