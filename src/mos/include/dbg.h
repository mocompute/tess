#ifndef MOS_DBG_H
#define MOS_DBG_H

#include "platform.h"

#ifndef NDEBUG

#ifndef MOS_WINDOWS
void dbg(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void dbg(char const *restrict fmt, ...);
#endif

#else

#define dbg(...) (void)0

#endif

#endif
