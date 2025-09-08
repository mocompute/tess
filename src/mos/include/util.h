#ifndef MOS_UTIL_H
#define MOS_UTIL_H

#define BIT(n)                       (1UL << (n))
#define SET_BIT(value, n)            ((value) |= BIT(n))
#define CLEAR_BIT(value, n)          ((value) &= ~BIT(n))
#define TOGGLE_BIT(value, n)         ((value) ^= BIT(n))
#define TEST_BIT(value, n)           (((value) & BIT(n)) != 0)

#define MASK(n)                      (BIT(n) - 1)
#define FIELD_MASK(start, len)       (MASK(len) << (start))
#define GET_FIELD(value, start, len) (((value) >> (start)) & MASK(len))
#define SET_FIELD(value, start, len, field)                                                                \
    ((value) = ((value) & ~FIELD_MASK(start, len)) | (((field) & MASK(len)) << (start)))

#endif
