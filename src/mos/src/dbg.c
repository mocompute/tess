#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>

#ifndef mos_dbg
void mos_dbg(char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);

    vfprintf(stderr, fmt, args);

    va_end(args);
}
#endif
