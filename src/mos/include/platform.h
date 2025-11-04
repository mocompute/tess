#ifndef MOS_PLATFORM_H
#define MOS_PLATFORM_H

#ifdef __WIN32__
#define MOS_WINDOWS
#endif

#ifdef __APPLE__
#define MOS_APPLE
#endif

#ifdef __linux__
#define MOS_LINUX
#endif

#ifdef MOS_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif

#endif
