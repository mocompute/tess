#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>

void dbg(char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"

    vfprintf(stderr, fmt, args);

#pragma clang diagnostic pop

    va_end(args);
}
