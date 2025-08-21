#ifndef MOS_DBG_H
#define MOS_DBG_H

#ifndef NDEBUG

void dbg(char const *restrict fmt, ...);

#else

#define dbg(...) (void)0

#endif

#endif
