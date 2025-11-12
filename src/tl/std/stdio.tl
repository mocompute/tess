#module stdio
#include <stdio.h>

_tl_fatal_      (s : String)   -> any
fatal           (s: String) { _tl_fatal(s) }

c_printf(fmt: String, args: ...) -> Void
