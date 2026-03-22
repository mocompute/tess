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

Slice[T]: { v: Ptr[T], size: CSize }

// Option type

Option[T]: | Some { v: T }
           | None

// Result type

Result[T, U]: | Ok  { v: T }
              | Err { v: U }
