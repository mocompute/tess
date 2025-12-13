#module builtin
#include <stddef.h>

// Symbols in the builtin module are available without qualification.

_tl_sizeof_     (x : T)        -> CSize
_tl_alignof_    (x : T)        -> CSize

Byte = CUnsignedChar

sizeof(T)  { _tl_sizeof_(T) }
alignof(T) { _tl_alignof_(T) }
