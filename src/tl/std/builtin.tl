#module builtin

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>              // for fprintf
#include <stdlib.h>             // for exit

#include <stddef.h>

// Symbols in the builtin module are available without qualification.

_tl_sizeof_[T] ()     -> CSize
_tl_sizeof_[T] (x: T) -> CSize
_tl_alignof_[T]()     -> CSize
_tl_alignof_[T](x: T) -> CSize

Int     = CLongLong
UInt    = CUnsignedLongLong
Float   = CDouble
Byte    = CUnsignedChar
CString = Ptr[CChar]

_tl_fatal_ (s: CString) -> any

sizeof[T] ()  -> CSize { _tl_sizeof_[T]()  }
sizeof    (x) -> CSize { _tl_sizeof_(x)    }
alignof[T]()  -> CSize { _tl_alignof_[T]() }
alignof   (x) -> CSize { _tl_alignof_(x)   }

// Option type

Option[T]: | Some { v: T }
           | None

// Result type

Result[T, U]: | Ok { v: T }
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
