#ifndef TESS_H
#define TESS_H

#include <stdnoreturn.h>

noreturn void fatal(char const *restrict, ...) __attribute__((format(printf, 1, 2)));

#endif
