#module fatal

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>
#include <stdlib.h>

_tl_fatal_      (s: CString)   -> any
