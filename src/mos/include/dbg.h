#ifndef MOS_DBG_H
#define MOS_DBG_H

#ifndef NDEBUG

void dbg(char const *restrict fmt, ...) __attribute__((format(printf, 1, 2)));

#else

#define dbg(...) (void)0

#endif

#endif
