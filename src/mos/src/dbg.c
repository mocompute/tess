#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>

void dbg(char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);

    vfprintf(stderr, fmt, args);

    va_end(args);
}
