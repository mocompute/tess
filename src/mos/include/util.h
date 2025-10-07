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
