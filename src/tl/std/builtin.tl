#module builtin

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>              // for fprintf
#include <stdlib.h>             // for exit

#include <stddef.h>
#include <string.h>

// Symbols in the builtin module are available without qualification.

#ifc
// Hash functions to support compiler codegen for hash/1 and _tl_hash_/1.

// Murmurhash3 fmix64 finalizer — mixes all bits so low bits (used for
// bucket selection) reflect the full key, preventing clustering.
static inline size_t tl_fmix64(size_t h) {
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}
static inline size_t tl_hash_bytes(const void *p, size_t n) {
    size_t h = 0xcbf29ce484222325ULL;
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return tl_fmix64(h);
}
static inline size_t tl_hash_cstring(const char *s) {
    size_t h = 0xcbf29ce484222325ULL;
    if (s) for (; *s; s++) { h ^= (unsigned char)*s; h *= 0x100000001b3ULL; }
    return tl_fmix64(h);
}
#endc


_tl_sizeof_[T] ()       -> CSize
_tl_sizeof_[T] (x: T)   -> CSize
_tl_alignof_[T]()       -> CSize
_tl_alignof_[T](x: T)   -> CSize
_tl_hash_[T]   (key: T) -> CSize

Int     = CLongLong
UInt    = CUnsignedLongLong
Float   = CDouble
Byte    = CUnsignedChar
CString = Ptr[CChar]

_tl_fatal_ (s: CString) -> any

sizeof[T] ()  -> CSize  { _tl_sizeof_[T]()  }
sizeof    (x) -> CSize { _tl_sizeof_(x)     }
alignof[T]()  -> CSize  { _tl_alignof_[T]() }
alignof   (x) -> CSize { _tl_alignof_(x)    }


// Option type

Option[T]: | Some { v: T }
           | None

// Result type

Result[T, U]: | Ok  { v: T }
              | Err { v: U }

// Unwrap: extract the value from an Option or Result, or return a default.
unwrap[T, U](opt_or_res, default: T) -> T {
    if opt_or_res :: Option[T] {
        case opt_or_res: Option[T] {
            s: Some { s.v }
            n: None { default }
        }
    } else if opt_or_res :: Result[T, U] {
        case opt_or_res: Result[T, U] {
            o: Ok { o.v }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_("unwrap: invalid type")
    }
}
