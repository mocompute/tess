#ifndef MOS_TYPES_H
#define MOS_TYPES_H

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

typedef unsigned char byte;

typedef int8_t        i8;
typedef int16_t       i16;
typedef int32_t       i32;
typedef int64_t       i64;

typedef uint8_t       u8;
typedef uint16_t      u16;
typedef uint32_t      u32;
typedef uint64_t      u64;

typedef float         f32;
typedef double        f64;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define null nullptr
#else
#define null NULL
#endif

#endif
