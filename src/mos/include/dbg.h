#ifndef MOS_DBG_H
#define MOS_DBG_H

#include "platform.h"

#ifndef NDEBUG

#ifndef MOS_WINDOWS
void mos_dbg(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void mos_dbg(char const *restrict fmt, ...);
#endif

#else

#define mos_dbg(...) (void)0

#endif

#endif
