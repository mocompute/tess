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

// Result type

Result(T, U): | Ok { v: T }
              | Err { v: U }

// Unwrap: extract the value from an Option or Result, or return a default.
unwrap(opt_or_res, default: T) -> T {
    if opt_or_res :: Option(T) {
        case opt_or_res: Option(T) {
            s: Some { s.v }
            n: None { default }
        }
    } else if opt_or_res :: Result(T, U) {
        case opt_or_res: Result(T, U) {
            o: Ok { o.v }
            e: Err { default }
        }
    }
    else {
        _tl_fatal_(c"unwrap: invalid type")
    }
}
