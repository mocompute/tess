#ifndef MOS_UTIL_H
#define MOS_UTIL_H

#define BIT(n)                       (1UL << (n))
#define BIT_SET(value, n)            ((value) |= BIT(n))
#define BIT_CLEAR(value, n)          ((value) &= ~BIT(n))
#define BIT_TOGGLE(value, n)         ((value) ^= BIT(n))
#define BIT_TEST(value, n)           (((value) & BIT(n)) != 0)

#define MASK(n)                      (BIT(n) - 1)
#define FIELD_MASK(start, len)       (MASK(len) << (start))
#define FIELD_GET(value, start, len) (((value) >> (start)) & MASK(len))
#define FIELD_SET(value, start, len, field)                                                                \
    ((value) = ((value) & ~FIELD_MASK(start, len)) | (((field) & MASK(len)) << (start)))

#ifdef _MSC_VER
// MSVC-compatible versions (no typeof or statement expressions)
// Note: min/max evaluate arguments multiple times - use with care
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
// swap uses memcpy to avoid needing typeof
#include <string.h>
#define swap(a, b)                                                                                         \
    do {                                                                                                   \
        unsigned char _swap_tmp[sizeof(a)];                                                                \
        memcpy(_swap_tmp, &(a), sizeof(a));                                                                \
        memcpy(&(a), &(b), sizeof(a));                                                                     \
        memcpy(&(b), _swap_tmp, sizeof(a));                                                                \
    } while (0)
#else
// GCC/Clang versions with typeof and statement expressions
#define max(a, b)                                                                                          \
    ({                                                                                                     \
        typeof(a) _a = (a);                                                                                \
        typeof(b) _b = (b);                                                                                \
        _a > _b ? _a : _b;                                                                                 \
    })

#define min(a, b)                                                                                          \
    ({                                                                                                     \
        typeof(a) _a = (a);                                                                                \
        typeof(b) _b = (b);                                                                                \
        _a < _b ? _a : _b;                                                                                 \
    })

#define swap(a, b)                                                                                         \
    do {                                                                                                   \
        typeof(a) _tmp = (a);                                                                              \
        (a)            = (b);                                                                              \
        (b)            = _tmp;                                                                             \
    } while (0)
#endif

#endif
