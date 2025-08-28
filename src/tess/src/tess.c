#include "tess.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void fatal(char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    exit(1);
}
