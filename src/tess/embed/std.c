// -- begin std --

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

int std_dbg(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
int std_dbg(char const *restrict fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    return 0;
}

// -- end std --
