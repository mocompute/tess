#module fatal

// invoking _tl_fatal_ requires fprintf() and exit()
#include <stdio.h>              // for fprintf
#include <stdlib.h>             // for exit

// Forward declaration moved to builtin.tl (after CString alias)
