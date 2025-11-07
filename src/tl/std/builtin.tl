#module builtin

// Symbols in the builtin module are available without qualification.

_tl_sizeof_     (x : T)        -> Int
_tl_fatal_      (s : String)   -> any

sizeof(T) { _tl_sizeof_(T) }
