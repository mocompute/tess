// Symbols in the builtin module are available without qualification.
#module builtin

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>              // for fprintf
#include <stdlib.h>             // for exit

#include <stddef.h>
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
