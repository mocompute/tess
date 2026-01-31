#module builtin
#include <stddef.h>

// Symbols in the builtin module are available without qualification.

_tl_sizeof_  (x: T) -> CSize
_tl_alignof_ (x: T) -> CSize

Byte    = CUnsignedChar
CString = Ptr(CChar)

sizeof (T) { _tl_sizeof_(T)  }
alignof(T) { _tl_alignof_(T) }

// Option type

Option(T): | Some { v: T }
           | None

unwrap(opt: Option(T), default: T) -> T {
    case opt: Option {
        s: Some { s.v }
        n: None { default }
    }
}

// Result type

Result(T, U): | Ok { v: T }
              | Err { v: U }
