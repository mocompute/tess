// Symbols in the builtin module are available without qualification.
#module builtin

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>              // for fprintf
#include <stdlib.h>             // for exit

#include <stddef.h>
#include <stdint.h>
#include <string.h>

Int     = CLongLong
UInt    = CUnsignedLongLong
Float   = CDouble
Byte    = CUnsignedChar
CString = Ptr[CChar]

_tl_sizeof_[T] ()     -> CSize
_tl_sizeof_[T] (x: T) -> CSize
_tl_alignof_[T]()     -> CSize
_tl_alignof_[T](x: T) -> CSize

sizeof[T] ()  -> CSize { _tl_sizeof_[T]()  }
sizeof    (x) -> CSize { _tl_sizeof_(x)    }
alignof[T]()  -> CSize { _tl_alignof_[T]() }
alignof   (x) -> CSize { _tl_alignof_(x)   }

_tl_fatal_ (s: CString) -> any

// -- Numeric cmp/eq helpers --
//
// Backing C functions for the parser-synthesized `cmp[T]` / `eq[T]` methods
// on builtin numeric type families (see src/tess/src/parser_numeric_ops.c).
// Type-erased on both pointer and size so one helper covers every width in a
// family — the synthesizer always passes `sizeof(T)` at the call site, which
// the C compiler constant-folds so each specialization is branch-free.

#ifc
static inline int tl_cmp_signed(const void *a, const void *b, size_t sz) {
    if (sz == sizeof(int8_t))  { int8_t  x = *(const int8_t *)a,  y = *(const int8_t *)b;  return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(int16_t)) { int16_t x = *(const int16_t *)a, y = *(const int16_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(int32_t)) { int32_t x = *(const int32_t *)a, y = *(const int32_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(int64_t)) { int64_t x = *(const int64_t *)a, y = *(const int64_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    return 0;
}
static inline int tl_cmp_unsigned(const void *a, const void *b, size_t sz) {
    if (sz == sizeof(uint8_t))  { uint8_t  x = *(const uint8_t *)a,  y = *(const uint8_t *)b;  return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(uint16_t)) { uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(uint32_t)) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    if (sz == sizeof(uint64_t)) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y ? 1 : 0; }
    return 0;
}
// NaN sorts greater than every non-NaN (including itself), qsort-style.
static inline int tl_cmp_float(const void *a, const void *b, size_t sz) {
    if (sz == sizeof(float))       { float       x = *(const float *)a,       y = *(const float *)b;       if (x < y) return -1; if (x > y) return 1; if (x == y) return 0; return (x == x) ? -1 : (y == y) ? 1 : 0; }
    if (sz == sizeof(double))      { double      x = *(const double *)a,      y = *(const double *)b;      if (x < y) return -1; if (x > y) return 1; if (x == y) return 0; return (x == x) ? -1 : (y == y) ? 1 : 0; }
    if (sz == sizeof(long double)) { long double x = *(const long double *)a, y = *(const long double *)b; if (x < y) return -1; if (x > y) return 1; if (x == y) return 0; return (x == x) ? -1 : (y == y) ? 1 : 0; }
    return 0;
}
static inline int tl_eq_bytes(const void *a, const void *b, size_t sz) {
    return memcmp(a, b, sz) == 0;
}
static inline int tl_eq_float(const void *a, const void *b, size_t sz) {
    if (sz == sizeof(float))       return *(const float       *)a == *(const float       *)b;
    if (sz == sizeof(double))      return *(const double      *)a == *(const double      *)b;
    if (sz == sizeof(long double)) return *(const long double *)a == *(const long double *)b;
    return 0;
}
#endc

c_tl_cmp_signed  (a: Ptr[any], b: Ptr[any], sz: CSize) -> CInt
c_tl_cmp_unsigned(a: Ptr[any], b: Ptr[any], sz: CSize) -> CInt
c_tl_cmp_float   (a: Ptr[any], b: Ptr[any], sz: CSize) -> CInt
c_tl_eq_bytes    (a: Ptr[any], b: Ptr[any], sz: CSize) -> Bool
// IEEE equality: NaN.eq(NaN) returns false, +0.0.eq(-0.0) returns true.
c_tl_eq_float    (a: Ptr[any], b: Ptr[any], sz: CSize) -> Bool

// Slice type — lightweight view over a contiguous array of elements.
// Used by variadic functions: the compiler packs variadic args into a stack-allocated
// array and passes a Slice to the function body.

Slice[T]: { data: Ptr[Const[T]], size: CSize }

// Option type

Option[T]: | Some { value: T }
           | None

// Result type

Result[T, U]: | Ok  { value: T }
              | Err { error: U }

or_else[T, U](opt_or_res, default: T) -> T {
    if opt_or_res :: Option[T] {
        case opt_or_res: Option[T] {
            s: Some { s.value }
            n: None { default }
        }
    } else if opt_or_res :: Result[T, U] {
        case opt_or_res: Result[T, U] {
            o: Ok { o.value }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_(c"or_else: invalid type")
    }
}

// String — Small-string-optimized, immutable string type.
//
// Strings of 14 bytes or fewer are stored inline (no heap allocation).
// Larger strings use a heap-allocated buffer via an explicit allocator.
//
// Defined in <builtin.tl> so string literals are always available.
#module String

_Big: {
    len: CSize,
    buf: Ptr[CChar],
}

_Small: {
    buf:     CArray[CChar, 15],
    tag_len: CUnsignedChar,
}

String: { | big: _Big
          | ss: _Small }

// Note: from_literal/1 is now a pseudo-function that is inlined at the transpiler phase. It still needs to
// have a type though.
from_literal(s: Ptr[Const[CChar]]) -> String

// -- ToBool --
//
// A type satisfying ToBool must provide a unary to_bool function returning Bool.
// Used as a variadic bound: check_all(args: ...ToBool) applies to_bool to each arg.

#module ToBool

ToBool[T]: { to_bool(a: T) -> Bool }

#module Int
to_bool(n) -> Bool { n != 0 }
#module UInt
to_bool(n) -> Bool { n != 0 }
#module Float
to_bool(f) -> Bool { f != 0.0 }
#module Bool
to_bool(b) -> Bool { b }
#module CString
to_bool(cs) -> Bool { cs != null && cs.[0] != '\0' }
#module String
to_bool(s) -> Bool { !s.is_empty() }
